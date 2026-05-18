// Redundant load elimination using conservative memory alias information.

#include "DLEPass.h"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BasicBlock.h"
#include "CallInst.h"
#include "Function.h"
#include "IRCFG.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "MemoryAlias.h"
#include "Module.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

using AvailableMemory = std::unordered_map<MemoryLocationKey, Value *, MemoryLocationKeyHash>;

bool sameAvailable(const AvailableMemory & lhs, const AvailableMemory & rhs)
{
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (const auto & entry: lhs) {
		auto iter = rhs.find(entry.first);
		if (iter == rhs.end() || iter->second != entry.second) {
			return false;
		}
	}
	return true;
}

void killMayAlias(AvailableMemory & available, MemoryAliasAnalysis & alias, const AliasLocation & location)
{
	if (!location.known) {
		available.clear();
		return;
	}

	for (auto iter = available.begin(); iter != available.end();) {
		if (alias.mayAlias(alias.locationFor(iter->first), location)) {
			iter = available.erase(iter);
		} else {
			++iter;
		}
	}
}

AvailableMemory intersectPredecessors(const IRCFG & cfg, const std::unordered_map<BasicBlock *, AvailableMemory> & out, BasicBlock * block)
{
	AvailableMemory result;
	auto predIter = cfg.predecessors.find(block);
	if (predIter == cfg.predecessors.end() || predIter->second.empty()) {
		return result;
	}

	bool first = true;
	for (auto * pred: predIter->second) {
		if (cfg.reachable.find(pred) == cfg.reachable.end()) {
			continue;
		}
		auto outIter = out.find(pred);
		if (outIter == out.end()) {
			continue;
		}

		if (first) {
			result = outIter->second;
			first = false;
			continue;
		}

		for (auto iter = result.begin(); iter != result.end();) {
			auto predValue = outIter->second.find(iter->first);
			if (predValue == outIter->second.end() || predValue->second != iter->second) {
				iter = result.erase(iter);
			} else {
				++iter;
			}
		}
	}

	return first ? AvailableMemory{} : result;
}

AvailableMemory transferBlock(
	BasicBlock * block,
	AvailableMemory available,
	MemoryAliasAnalysis & alias,
	std::vector<std::pair<LoadInst *, Value *>> * replacements)
{
	for (auto * inst: block->getInstructions()) {
		if (dynamic_cast<CallInst *>(inst) != nullptr) {
			available.clear();
			continue;
		}

		if (auto * store = dynamic_cast<StoreInst *>(inst); store != nullptr) {
			auto location = alias.location(store->getPointerOperand());
			killMayAlias(available, alias, location);
			if (alias.isExact(location)) {
				available[alias.keyFor(location)] = store->getValueOperand();
			}
			continue;
		}

		if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
			auto location = alias.location(load->getPointerOperand());
			if (!alias.isExact(location)) {
				continue;
			}

			const auto key = alias.keyFor(location);
			auto availableValue = available.find(key);
			if (availableValue != available.end()) {
				if (replacements != nullptr) {
					replacements->push_back({load, availableValue->second});
				}
				continue;
			}
			available[key] = load;
		}
	}

	return available;
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

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin() || function->getEntryBlock() == nullptr) {
		return false;
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	std::unordered_map<BasicBlock *, AvailableMemory> in;
	std::unordered_map<BasicBlock *, AvailableMemory> out;

	bool changed = true;
	while (changed) {
		changed = false;
		MemoryAliasAnalysis alias(function);
		for (auto * block: cfg.blocks) {
			if (cfg.reachable.find(block) == cfg.reachable.end()) {
				continue;
			}

			AvailableMemory newIn;
			if (block != function->getEntryBlock()) {
				newIn = intersectPredecessors(cfg, out, block);
			}
			if (!sameAvailable(in[block], newIn)) {
				in[block] = newIn;
				changed = true;
			}

			auto newOut = transferBlock(block, newIn, alias, nullptr);
			if (!sameAvailable(out[block], newOut)) {
				out[block] = newOut;
				changed = true;
			}
		}
	}

	std::vector<std::pair<LoadInst *, Value *>> replacements;
	MemoryAliasAnalysis alias(function);
	for (auto * block: cfg.blocks) {
		if (cfg.reachable.find(block) == cfg.reachable.end()) {
			continue;
		}
		(void) transferBlock(block, in[block], alias, &replacements);
	}

	bool modified = false;
	for (auto & replacement: replacements) {
		auto * load = replacement.first;
		auto * value = replacement.second;
		if (load == nullptr || value == nullptr || load == value) {
			continue;
		}
		load->replaceAllUseWith(value);
		modified |= eraseInstruction(function, load);
	}
	return modified;
}

} // namespace

bool DLEPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
