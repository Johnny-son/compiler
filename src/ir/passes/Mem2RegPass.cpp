// Promote eligible stack slots to SSA values.

#include "Mem2RegPass.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "IRCFG.h"
#include "ir/Instructions/AllocaInst.h"
#include "ir/Instructions/LoadInst.h"
#include "ir/Instructions/PhiInst.h"
#include "ir/Instructions/StoreInst.h"
#include "ir/Types/FloatType.h"
#include "ir/include/Function.h"
#include "ir/include/Instruction.h"
#include "ir/include/Module.h"
#include "ir/include/Type.h"
#include "ir/include/Value.h"

namespace {

using AllocaSet = std::unordered_set<AllocaInst *>;
using BlockSet = std::unordered_set<BasicBlock *>;
using PhiMap = std::unordered_map<PhiInst *, AllocaInst *>;
using StackMap = std::unordered_map<AllocaInst *, std::vector<Value *>>;

bool isPromotableType(Type * type)
{
	return type != nullptr && (type->isIntegerType() || type->isFloatType());
}

std::string phiNameFor(AllocaInst * alloca)
{
	if (alloca == nullptr) {
		return "mem2reg";
	}
	if (!alloca->getName().empty()) {
		return alloca->getName();
	}

	std::string name = alloca->getIRName();
	if (!name.empty() && name[0] == '%') {
		name.erase(name.begin());
	}
	const std::string suffix = ".addr";
	if (name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix) {
		name.erase(name.size() - suffix.size());
	}
	return name.empty() ? "mem2reg" : name;
}

Value * zeroValueFor(Module & module, Type * type)
{
	if (type != nullptr && type->isFloatType()) {
		return module.newConstFloat(0.0f);
	}
	return module.newConstInt(0);
}

bool userBlockIsReachable(const IRCFG & cfg, Value * value)
{
	for (auto * use: value->getUseList()) {
		auto * inst = dynamic_cast<Instruction *>(use->getUser());
		if (inst == nullptr) {
			return false;
		}
		auto blockIter = cfg.instructionBlock.find(inst);
		if (blockIter == cfg.instructionBlock.end() || cfg.reachable.find(blockIter->second) == cfg.reachable.end()) {
			return false;
		}
	}
	return true;
}

bool isPromotableAlloca(AllocaInst * alloca, const IRCFG & cfg)
{
	if (alloca == nullptr || !isPromotableType(alloca->getAllocatedType())) {
		return false;
	}

	if (!userBlockIsReachable(cfg, alloca)) {
		return false;
	}

	for (auto * use: alloca->getUseList()) {
		auto * inst = dynamic_cast<Instruction *>(use->getUser());
		if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
			if (load->getPointerOperand() != alloca) {
				return false;
			}
			continue;
		}

		if (auto * store = dynamic_cast<StoreInst *>(inst); store != nullptr) {
			if (store->getPointerOperand() != alloca || store->getValueOperand() == alloca) {
				return false;
			}
			continue;
		}

		return false;
	}

	return true;
}

std::vector<AllocaInst *> collectPromotableAllocas(Function * function, const IRCFG & cfg, int & skippedAllocas)
{
	std::vector<AllocaInst *> allocas;
	if (function == nullptr) {
		return allocas;
	}

	for (auto * block: function->getBasicBlocks()) {
		for (auto * inst: block->getInstructions()) {
			auto * alloca = dynamic_cast<AllocaInst *>(inst);
			if (alloca == nullptr) {
				continue;
			}

			if (isPromotableAlloca(alloca, cfg)) {
				allocas.push_back(alloca);
			} else {
				++skippedAllocas;
			}
		}
	}

	return allocas;
}

std::vector<BasicBlock *> storeBlocksFor(AllocaInst * alloca, const IRCFG & cfg)
{
	std::vector<BasicBlock *> blocks;
	BlockSet seen;
	for (auto * use: alloca->getUseList()) {
		auto * store = dynamic_cast<StoreInst *>(use->getUser());
		if (store == nullptr || store->getPointerOperand() != alloca) {
			continue;
		}

		auto blockIter = cfg.instructionBlock.find(store);
		if (blockIter != cfg.instructionBlock.end() && seen.insert(blockIter->second).second) {
			blocks.push_back(blockIter->second);
		}
	}
	return blocks;
}

PhiInst * findPhiForAlloca(BasicBlock * block, AllocaInst * alloca, const PhiMap & phiToAlloca)
{
	if (block == nullptr || alloca == nullptr) {
		return nullptr;
	}

	for (auto * inst: block->getInstructions()) {
		auto * phi = dynamic_cast<PhiInst *>(inst);
		if (phi == nullptr) {
			break;
		}
		auto iter = phiToAlloca.find(phi);
		if (iter != phiToAlloca.end() && iter->second == alloca) {
			return phi;
		}
	}
	return nullptr;
}

PhiInst * insertPhi(Function * function, BasicBlock * block, AllocaInst * alloca)
{
	auto * phi = new PhiInst(function, alloca->getAllocatedType());
	phi->setIRName(function->allocateLocalName(phiNameFor(alloca)));

	auto & instructions = block->getInstructions();
	auto insertPos = instructions.begin();
	while (insertPos != instructions.end() && dynamic_cast<PhiInst *>(*insertPos) != nullptr) {
		++insertPos;
	}
	instructions.insert(insertPos, phi);
	return phi;
}

void removeInstruction(BasicBlock * block, Instruction * inst)
{
	if (block == nullptr || inst == nullptr) {
		return;
	}

	auto & instructions = block->getInstructions();
	auto iter = std::find(instructions.begin(), instructions.end(), inst);
	if (iter != instructions.end()) {
		instructions.erase(iter);
	}

	inst->clearOperands();
	inst->removeUses();
	delete inst;
}

Value * currentValue(AllocaInst * alloca, StackMap & stacks, Module & module)
{
	auto & stack = stacks[alloca];
	if (!stack.empty()) {
		return stack.back();
	}
	return zeroValueFor(module, alloca->getAllocatedType());
}

void addIncomingValues(
	BasicBlock * block,
	const IRCFG & cfg,
	PhiMap & phiToAlloca,
	StackMap & stacks,
	Module & module)
{
	auto succIter = cfg.successors.find(block);
	if (succIter == cfg.successors.end()) {
		return;
	}

	for (auto * succ: succIter->second) {
		for (auto * inst: succ->getInstructions()) {
			auto * phi = dynamic_cast<PhiInst *>(inst);
			if (phi == nullptr) {
				break;
			}

			auto phiIter = phiToAlloca.find(phi);
			if (phiIter == phiToAlloca.end()) {
				continue;
			}

			phi->addIncoming(currentValue(phiIter->second, stacks, module), block);
		}
	}
}

bool phiHasIncomingFrom(PhiInst * phi, BasicBlock * pred)
{
	for (const auto & incoming: phi->getIncomingValues()) {
		if (incoming.second == pred) {
			return true;
		}
	}
	return false;
}

void completePhiIncomingValues(const IRCFG & cfg, const PhiMap & phiToAlloca, Module & module)
{
	for (auto * block: cfg.blocks) {
		auto predIter = cfg.predecessors.find(block);
		if (predIter == cfg.predecessors.end()) {
			continue;
		}

		for (auto * inst: block->getInstructions()) {
			auto * phi = dynamic_cast<PhiInst *>(inst);
			if (phi == nullptr) {
				break;
			}

			auto phiIter = phiToAlloca.find(phi);
			if (phiIter == phiToAlloca.end()) {
				continue;
			}

			for (auto * pred: predIter->second) {
				if (!phiHasIncomingFrom(phi, pred)) {
					phi->addIncoming(zeroValueFor(module, phiIter->second->getAllocatedType()), pred);
				}
			}
		}
	}
}

void renameBlock(
	BasicBlock * block,
	const IRCFG & cfg,
	const DominanceInfo & dominance,
	const AllocaSet & promoted,
	PhiMap & phiToAlloca,
	StackMap & stacks,
	Module & module,
	std::vector<Instruction *> & deadInstructions,
	std::vector<AllocaInst *> & deadAllocas,
	std::unordered_map<Instruction *, BasicBlock *> & deadInstBlocks)
{
	std::vector<AllocaInst *> pushed;

	for (auto * inst: block->getInstructions()) {
		auto * phi = dynamic_cast<PhiInst *>(inst);
		if (phi == nullptr) {
			break;
		}

		auto phiIter = phiToAlloca.find(phi);
		if (phiIter == phiToAlloca.end()) {
			continue;
		}

		stacks[phiIter->second].push_back(phi);
		pushed.push_back(phiIter->second);
	}

	for (auto * inst: block->getInstructions()) {
		if (dynamic_cast<PhiInst *>(inst) != nullptr) {
			continue;
		}

		if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
			auto * alloca = dynamic_cast<AllocaInst *>(load->getPointerOperand());
			if (alloca != nullptr && promoted.find(alloca) != promoted.end()) {
				load->replaceAllUseWith(currentValue(alloca, stacks, module));
				deadInstructions.push_back(load);
				deadInstBlocks[load] = block;
			}
			continue;
		}

		if (auto * store = dynamic_cast<StoreInst *>(inst); store != nullptr) {
			auto * alloca = dynamic_cast<AllocaInst *>(store->getPointerOperand());
			if (alloca != nullptr && promoted.find(alloca) != promoted.end()) {
				stacks[alloca].push_back(store->getValueOperand());
				pushed.push_back(alloca);
				deadInstructions.push_back(store);
				deadInstBlocks[store] = block;
			}
		}
	}

	addIncomingValues(block, cfg, phiToAlloca, stacks, module);

	auto childIter = dominance.dominatorTreeChildren.find(block);
	if (childIter != dominance.dominatorTreeChildren.end()) {
		for (auto * child: childIter->second) {
			renameBlock(
				child,
				cfg,
				dominance,
				promoted,
				phiToAlloca,
				stacks,
				module,
				deadInstructions,
				deadAllocas,
				deadInstBlocks);
		}
	}

	for (auto iter = pushed.rbegin(); iter != pushed.rend(); ++iter) {
		auto stackIter = stacks.find(*iter);
		if (stackIter != stacks.end() && !stackIter->second.empty()) {
			stackIter->second.pop_back();
		}
	}

	(void) deadAllocas;
}

void insertPhiNodes(
	Function * function,
	const std::vector<AllocaInst *> & allocas,
	const IRCFG & cfg,
	const DominanceInfo & dominance,
	PhiMap & phiToAlloca,
	int & insertedPhis)
{
	for (auto * alloca: allocas) {
		auto defBlocks = storeBlocksFor(alloca, cfg);
		std::vector<BasicBlock *> worklist = defBlocks;
		BlockSet queued{worklist.begin(), worklist.end()};
		BlockSet hasPhi;

		while (!worklist.empty()) {
			auto * block = worklist.back();
			worklist.pop_back();

			auto frontierIter = dominance.dominanceFrontier.find(block);
			if (frontierIter == dominance.dominanceFrontier.end()) {
				continue;
			}

			for (auto * frontierBlock: frontierIter->second) {
				if (hasPhi.find(frontierBlock) != hasPhi.end()) {
					continue;
				}

				if (findPhiForAlloca(frontierBlock, alloca, phiToAlloca) != nullptr) {
					hasPhi.insert(frontierBlock);
					continue;
				}

				auto * phi = insertPhi(function, frontierBlock, alloca);
				phiToAlloca[phi] = alloca;
				hasPhi.insert(frontierBlock);
				++insertedPhis;

				if (queued.insert(frontierBlock).second) {
					worklist.push_back(frontierBlock);
				}
			}
		}
	}
}

} // namespace

bool Mem2RegPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		if (function == nullptr || function->isBuiltin()) {
			continue;
		}

		IRCFG cfg = IRCFGBuilder::build(function);
		BasicBlock * entry = function->getEntryBlock();
		if (entry == nullptr || cfg.reachable.find(entry) == cfg.reachable.end()) {
			continue;
		}

		DominanceInfo dominance = DominanceBuilder::build(cfg, entry);
		auto promotableAllocas = collectPromotableAllocas(function, cfg, skippedAllocas);
		if (promotableAllocas.empty()) {
			continue;
		}

		PhiMap phiToAlloca;
		insertPhiNodes(function, promotableAllocas, cfg, dominance, phiToAlloca, insertedPhis);

		AllocaSet promoted{promotableAllocas.begin(), promotableAllocas.end()};
		StackMap stacks;
		std::vector<Instruction *> deadInstructions;
		std::vector<AllocaInst *> deadAllocas = promotableAllocas;
		std::unordered_map<Instruction *, BasicBlock *> deadInstBlocks;

		renameBlock(
			entry,
			cfg,
			dominance,
			promoted,
			phiToAlloca,
			stacks,
			module,
			deadInstructions,
			deadAllocas,
			deadInstBlocks);
		completePhiIncomingValues(cfg, phiToAlloca, module);

		for (auto * inst: deadInstructions) {
			auto blockIter = deadInstBlocks.find(inst);
			if (blockIter == deadInstBlocks.end()) {
				continue;
			}
			if (dynamic_cast<LoadInst *>(inst) != nullptr) {
				++removedLoads;
			} else if (dynamic_cast<StoreInst *>(inst) != nullptr) {
				++removedStores;
			}
			removeInstruction(blockIter->second, inst);
		}

		for (auto * alloca: deadAllocas) {
			auto blockIter = cfg.instructionBlock.find(alloca);
			if (blockIter == cfg.instructionBlock.end()) {
				continue;
			}
			removeInstruction(blockIter->second, alloca);
			++promotedAllocas;
		}
	}

	return true;
}
