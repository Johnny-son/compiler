// Conservative dead store elimination.

#include "DSEPass.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "ArrayType.h"
#include "BasicBlock.h"
#include "CallInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "IRCFG.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "MemoryAlias.h"
#include "Module.h"
#include "StoreInst.h"
#include "Use.h"
#include "User.h"
#include "Value.h"

namespace {

using KnownMemory = std::unordered_map<MemoryLocationKey, Value *, MemoryLocationKeyHash>;
using LiveLocations = std::unordered_set<MemoryLocationKey, MemoryLocationKeyHash>;

struct PendingStore {
	StoreInst * store = nullptr;
	AliasLocation location;
};

void killMayAlias(KnownMemory & memory, MemoryAliasAnalysis & alias, const AliasLocation & location)
{
	if (!location.known) {
		memory.clear();
		return;
	}
	for (auto iter = memory.begin(); iter != memory.end();) {
		if (alias.mayAlias(alias.locationFor(iter->first), location)) {
			iter = memory.erase(iter);
		} else {
			++iter;
		}
	}
}

void erasePendingMayAlias(std::vector<PendingStore> & pending, MemoryAliasAnalysis & alias, const AliasLocation & location)
{
	if (!location.known) {
		pending.clear();
		return;
	}
	pending.erase(
		std::remove_if(
			pending.begin(),
			pending.end(),
			[&](const PendingStore & candidate) { return alias.mayAlias(candidate.location, location); }),
		pending.end());
}

bool eraseInstruction(Function * function, Instruction * inst)
{
	if (function == nullptr || inst == nullptr) {
		return false;
	}
	for (auto * block: function->getBasicBlocks()) {
		auto & instructions = block->getInstructions();
		auto iter = std::find(instructions.begin(), instructions.end(), inst);
		if (iter == instructions.end()) {
			continue;
		}
		instructions.erase(iter);
		inst->clearOperands();
		inst->removeUses();
		delete inst;
		return true;
	}
	return false;
}

bool isArrayAlloca(AllocaInst * alloca)
{
	return alloca != nullptr && alloca->getAllocatedType() != nullptr && alloca->getAllocatedType()->isArrayType();
}

bool allocaEscapes(AllocaInst * alloca)
{
	if (alloca == nullptr) {
		return true;
	}

	std::vector<Value *> worklist{alloca};
	std::unordered_set<Value *> visited;
	while (!worklist.empty()) {
		auto * value = worklist.back();
		worklist.pop_back();
		if (!visited.insert(value).second) {
			continue;
		}

		for (auto * use: value->getUseList()) {
			auto * user = dynamic_cast<User *>(use->getUser());
			auto * inst = dynamic_cast<Instruction *>(user);
			if (inst == nullptr) {
				return true;
			}

			if (dynamic_cast<GetElementPtrInst *>(inst) != nullptr) {
				worklist.push_back(inst);
				continue;
			}

			if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
				if (load->getPointerOperand() == value) {
					continue;
				}
				return true;
			}

			if (auto * store = dynamic_cast<StoreInst *>(inst); store != nullptr) {
				if (store->getPointerOperand() == value) {
					continue;
				}
				return true;
			}

			return true;
		}
	}
	return false;
}

std::unordered_set<Value *> collectNonEscapedArrayAllocas(Function * function)
{
	std::unordered_set<Value *> result;
	for (auto * block: function->getBasicBlocks()) {
		for (auto * inst: block->getInstructions()) {
			auto * alloca = dynamic_cast<AllocaInst *>(inst);
			if (isArrayAlloca(alloca) && !allocaEscapes(alloca)) {
				result.insert(alloca);
			}
		}
	}
	return result;
}

bool sameLiveSet(const LiveLocations & lhs, const LiveLocations & rhs)
{
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (const auto & value: lhs) {
		if (rhs.find(value) == rhs.end()) {
			return false;
		}
	}
	return true;
}

bool isTrackedLocation(const AliasLocation & location, const std::unordered_set<Value *> & trackedBases, MemoryAliasAnalysis & alias)
{
	return alias.isExact(location) && trackedBases.find(location.base) != trackedBases.end();
}

LiveLocations unionSuccessors(const IRCFG & cfg, const std::unordered_map<BasicBlock *, LiveLocations> & in, BasicBlock * block)
{
	LiveLocations result;
	auto succIter = cfg.successors.find(block);
	if (succIter == cfg.successors.end()) {
		return result;
	}
	for (auto * succ: succIter->second) {
		if (cfg.reachable.find(succ) == cfg.reachable.end()) {
			continue;
		}
		auto inIter = in.find(succ);
		if (inIter == in.end()) {
			continue;
		}
		result.insert(inIter->second.begin(), inIter->second.end());
	}
	return result;
}

void addAllForBase(LiveLocations & live, const LiveLocations & allTrackedLocations, Value * base)
{
	for (const auto & key: allTrackedLocations) {
		if (key.base == base) {
			live.insert(key);
		}
	}
}

LiveLocations transferBackward(
	BasicBlock * block,
	LiveLocations live,
	MemoryAliasAnalysis & alias,
	const std::unordered_set<Value *> & trackedBases,
	const LiveLocations & allTrackedLocations,
	std::unordered_set<Instruction *> * remove)
{
	auto & instructions = block->getInstructions();
	for (auto iter = instructions.rbegin(); iter != instructions.rend(); ++iter) {
		auto * inst = *iter;

		if (dynamic_cast<CallInst *>(inst) != nullptr) {
			continue;
		}

		if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
			auto location = alias.location(load->getPointerOperand());
			if (isTrackedLocation(location, trackedBases, alias)) {
				live.insert(alias.keyFor(location));
			} else if (location.known && trackedBases.find(location.base) != trackedBases.end()) {
				addAllForBase(live, allTrackedLocations, location.base);
			} else if (!location.known) {
				live.insert(allTrackedLocations.begin(), allTrackedLocations.end());
			}
			continue;
		}

		auto * store = dynamic_cast<StoreInst *>(inst);
		if (store == nullptr) {
			continue;
		}

		auto location = alias.location(store->getPointerOperand());
		if (!isTrackedLocation(location, trackedBases, alias)) {
			continue;
		}

		auto key = alias.keyFor(location);
		if (live.find(key) == live.end()) {
			if (remove != nullptr) {
				remove->insert(store);
			}
		} else {
			live.erase(key);
		}
	}
	return live;
}

void runBackwardLocalArrayDSE(Function * function, std::unordered_set<Instruction *> & remove)
{
	auto trackedBases = collectNonEscapedArrayAllocas(function);
	if (trackedBases.empty()) {
		return;
	}

	MemoryAliasAnalysis alias(function);
	LiveLocations allTrackedLocations;
	for (auto * block: function->getBasicBlocks()) {
		for (auto * inst: block->getInstructions()) {
			AliasLocation location;
			if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
				location = alias.location(load->getPointerOperand());
			} else if (auto * store = dynamic_cast<StoreInst *>(inst); store != nullptr) {
				location = alias.location(store->getPointerOperand());
			} else {
				continue;
			}
			if (isTrackedLocation(location, trackedBases, alias)) {
				allTrackedLocations.insert(alias.keyFor(location));
			}
		}
	}
	if (allTrackedLocations.empty()) {
		return;
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	std::unordered_map<BasicBlock *, LiveLocations> in;
	std::unordered_map<BasicBlock *, LiveLocations> out;

	bool changed = true;
	while (changed) {
		changed = false;
		MemoryAliasAnalysis iterationAlias(function);
		for (auto blockIter = cfg.blocks.rbegin(); blockIter != cfg.blocks.rend(); ++blockIter) {
			auto * block = *blockIter;
			if (cfg.reachable.find(block) == cfg.reachable.end()) {
				continue;
			}

			auto newOut = unionSuccessors(cfg, in, block);
			if (!sameLiveSet(out[block], newOut)) {
				out[block] = newOut;
				changed = true;
			}

			auto newIn = transferBackward(block, newOut, iterationAlias, trackedBases, allTrackedLocations, nullptr);
			if (!sameLiveSet(in[block], newIn)) {
				in[block] = newIn;
				changed = true;
			}
		}
	}

	MemoryAliasAnalysis finalAlias(function);
	for (auto * block: cfg.blocks) {
		if (cfg.reachable.find(block) == cfg.reachable.end()) {
			continue;
		}
		(void) transferBackward(block, out[block], finalAlias, trackedBases, allTrackedLocations, &remove);
	}
}

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	MemoryAliasAnalysis alias(function);
	std::unordered_set<Instruction *> remove;

	for (auto * block: function->getBasicBlocks()) {
		std::vector<PendingStore> pending;
		KnownMemory knownMemory;

		for (auto * inst: block->getInstructions()) {
			if (dynamic_cast<CallInst *>(inst) != nullptr) {
				pending.clear();
				knownMemory.clear();
				continue;
			}

			if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
				auto location = alias.location(load->getPointerOperand());
				erasePendingMayAlias(pending, alias, location);
				if (alias.isExact(location)) {
					knownMemory[alias.keyFor(location)] = load;
				}
				continue;
			}

			auto * store = dynamic_cast<StoreInst *>(inst);
			if (store == nullptr) {
				continue;
			}

			auto location = alias.location(store->getPointerOperand());
			if (!location.known) {
				pending.clear();
				knownMemory.clear();
				continue;
			}

			if (alias.isExact(location)) {
				auto key = alias.keyFor(location);
				auto known = knownMemory.find(key);
				if (known != knownMemory.end() && known->second == store->getValueOperand()) {
					remove.insert(store);
					continue;
				}
			}

			for (auto iter = pending.begin(); iter != pending.end();) {
				if (alias.mustAlias(iter->location, location)) {
					remove.insert(iter->store);
					iter = pending.erase(iter);
				} else {
					++iter;
				}
			}

			killMayAlias(knownMemory, alias, location);
			if (alias.isExact(location)) {
				knownMemory[alias.keyFor(location)] = store->getValueOperand();
				pending.push_back(PendingStore{store, location});
			} else {
				erasePendingMayAlias(pending, alias, location);
			}
		}
	}

	runBackwardLocalArrayDSE(function, remove);

	bool modified = false;
	for (auto * inst: remove) {
		modified |= eraseInstruction(function, inst);
	}
	return modified;
}

} // namespace

bool DSEPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
