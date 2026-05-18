// Forward loads from stack slots initialized by one dominating store.

#include "SingleStoreAllocaPass.h"

#include <algorithm>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "IRCFG.h"
#include "Function.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "Module.h"
#include "StoreInst.h"
#include "Use.h"
#include "Value.h"

namespace {

struct Candidate {
	AllocaInst * alloca = nullptr;
	StoreInst * store = nullptr;
	std::vector<LoadInst *> loads;
};

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

bool appearsBefore(BasicBlock * block, Instruction * before, Instruction * after)
{
	if (block == nullptr || before == nullptr || after == nullptr) {
		return false;
	}

	const auto & instructions = block->getInstructions();
	auto beforeIter = std::find(instructions.begin(), instructions.end(), before);
	auto afterIter = std::find(instructions.begin(), instructions.end(), after);
	return beforeIter != instructions.end() && afterIter != instructions.end() && beforeIter < afterIter;
}

bool storeDominatesLoad(StoreInst * store, LoadInst * load, const IRCFG & cfg, const DominanceInfo & dominance)
{
	auto storeBlockIter = cfg.instructionBlock.find(store);
	auto loadBlockIter = cfg.instructionBlock.find(load);
	if (storeBlockIter == cfg.instructionBlock.end() || loadBlockIter == cfg.instructionBlock.end()) {
		return false;
	}

	auto * storeBlock = storeBlockIter->second;
	auto * loadBlock = loadBlockIter->second;
	if (storeBlock == loadBlock) {
		return appearsBefore(storeBlock, store, load);
	}
	return dominance.dominates(storeBlock, loadBlock);
}

bool inspectAlloca(AllocaInst * alloca, Candidate & candidate)
{
	if (alloca == nullptr) {
		return false;
	}

	candidate.alloca = alloca;
	std::vector<Use *> uses{alloca->getUseList().begin(), alloca->getUseList().end()};
	for (auto * use: uses) {
		auto * inst = dynamic_cast<Instruction *>(use->getUser());
		if (inst == nullptr) {
			return false;
		}

		if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
			if (load->getPointerOperand() != alloca) {
				return false;
			}
			candidate.loads.push_back(load);
			continue;
		}

		if (auto * store = dynamic_cast<StoreInst *>(inst); store != nullptr) {
			if (store->getPointerOperand() != alloca || store->getValueOperand() == alloca) {
				return false;
			}
			if (candidate.store != nullptr) {
				return false;
			}
			candidate.store = store;
			continue;
		}

		return false;
	}

	return candidate.store != nullptr && !candidate.loads.empty();
}

bool isSafeCandidate(const Candidate & candidate, const IRCFG & cfg, const DominanceInfo & dominance)
{
	if (candidate.alloca == nullptr || candidate.store == nullptr || candidate.loads.empty()) {
		return false;
	}

	for (auto * load: candidate.loads) {
		if (!storeDominatesLoad(candidate.store, load, cfg, dominance)) {
			return false;
		}
	}
	return true;
}

std::vector<AllocaInst *> collectAllocas(Function * function)
{
	std::vector<AllocaInst *> allocas;
	if (function == nullptr) {
		return allocas;
	}

	for (auto * block: function->getBasicBlocks()) {
		for (auto * inst: block->getInstructions()) {
			auto * alloca = dynamic_cast<AllocaInst *>(inst);
			if (alloca != nullptr) {
				allocas.push_back(alloca);
			}
		}
	}
	return allocas;
}

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	auto * entry = function->getEntryBlock();
	if (entry == nullptr) {
		return false;
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	if (cfg.reachable.find(entry) == cfg.reachable.end()) {
		return false;
	}
	DominanceInfo dominance = DominanceBuilder::build(cfg, entry);

	bool changed = false;
	for (auto * alloca: collectAllocas(function)) {
		Candidate candidate;
		if (!inspectAlloca(alloca, candidate) || !isSafeCandidate(candidate, cfg, dominance)) {
			continue;
		}

		Value * storedValue = candidate.store->getValueOperand();
		for (auto * load: candidate.loads) {
			auto blockIter = cfg.instructionBlock.find(load);
			if (blockIter == cfg.instructionBlock.end()) {
				continue;
			}
			load->replaceAllUseWith(storedValue);
			removeInstruction(blockIter->second, load);
		}

		auto storeBlockIter = cfg.instructionBlock.find(candidate.store);
		if (storeBlockIter != cfg.instructionBlock.end()) {
			removeInstruction(storeBlockIter->second, candidate.store);
		}

		auto allocaBlockIter = cfg.instructionBlock.find(candidate.alloca);
		if (allocaBlockIter != cfg.instructionBlock.end()) {
			removeInstruction(allocaBlockIter->second, candidate.alloca);
		}

		changed = true;
	}
	return changed;
}

} // namespace

bool SingleStoreAllocaPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
