// Conservative loop-invariant code motion.

#include "LICMPass.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CastInst.h"
#include "FCmpInst.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "IRCFG.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "LoopAnalysis.h"
#include "ZExtInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "StoreInst.h"

namespace {

using BlockSet = std::unordered_set<BasicBlock *>;
using InstSet = std::unordered_set<Instruction *>;

bool isSafeBinary(BinaryInst * inst)
{
	if (inst == nullptr) {
		return false;
	}

	switch (inst->getBinaryOp()) {
		case BinaryInst::Op::Add:
		case BinaryInst::Op::Sub:
		case BinaryInst::Op::Mul:
		case BinaryInst::Op::FAdd:
		case BinaryInst::Op::FSub:
		case BinaryInst::Op::FMul:
			return true;
		case BinaryInst::Op::SDiv:
		case BinaryInst::Op::SRem:
		case BinaryInst::Op::FDiv:
			return false;
	}
	return false;
}

bool isHoistableInstruction(Instruction * inst)
{
	if (inst == nullptr || inst->isTerminator()) {
		return false;
	}
	if (auto * binary = dynamic_cast<BinaryInst *>(inst); binary != nullptr) {
		return isSafeBinary(binary);
	}
	return dynamic_cast<CastInst *>(inst) != nullptr || dynamic_cast<FCmpInst *>(inst) != nullptr ||
		   dynamic_cast<GetElementPtrInst *>(inst) != nullptr || dynamic_cast<ICmpInst *>(inst) != nullptr ||
		   dynamic_cast<ZExtInst *>(inst) != nullptr;
}

bool loopContainsCall(const IRLoopInfo & loop)
{
	for (auto * block: loop.blocks) {
		for (auto * inst: block->getInstructions()) {
			if (dynamic_cast<CallInst *>(inst) != nullptr) {
				return true;
			}
		}
	}
	return false;
}

bool functionStoresToGlobal(Function * function, GlobalVariable * global)
{
	if (function == nullptr || global == nullptr) {
		return true;
	}

	for (auto * block: function->getBasicBlocks()) {
		for (auto * inst: block->getInstructions()) {
			auto * store = dynamic_cast<StoreInst *>(inst);
			if (store != nullptr && store->getPointerOperand() == global) {
				return true;
			}
		}
	}
	return false;
}

bool isHoistableGlobalLoad(Function * function, const IRLoopInfo & loop, LoadInst * inst)
{
	if (inst == nullptr || inst->getType() == nullptr) {
		return false;
	}
	if (!inst->getType()->isInt32Type() && !inst->getType()->isFloatType()) {
		return false;
	}

	auto * global = dynamic_cast<GlobalVariable *>(inst->getPointerOperand());
	if (global == nullptr) {
		return false;
	}
	if (loopContainsCall(loop) || functionStoresToGlobal(function, global)) {
		return false;
	}
	return true;
}

bool isLoopInvariantOperand(
	Value * value,
	const IRLoopInfo & loop,
	const IRCFG & cfg,
	const DominanceInfo & dominance,
	const InstSet & hoisted)
{
	auto * definingInst = dynamic_cast<Instruction *>(value);
	if (definingInst == nullptr) {
		return true;
	}
	if (hoisted.find(definingInst) != hoisted.end()) {
		return true;
	}

	auto blockIter = cfg.instructionBlock.find(definingInst);
	if (blockIter == cfg.instructionBlock.end()) {
		return true;
	}
	if (loop.blocks.find(blockIter->second) != loop.blocks.end()) {
		return false;
	}
	return dominance.dominates(blockIter->second, loop.preheader);
}

bool isLoopInvariantInstruction(
	Function * function,
	Instruction * inst,
	const IRLoopInfo & loop,
	const IRCFG & cfg,
	const DominanceInfo & dominance,
	const InstSet & hoisted)
{
	auto * load = dynamic_cast<LoadInst *>(inst);
	if (load != nullptr) {
		if (!isHoistableGlobalLoad(function, loop, load)) {
			return false;
		}
	} else if (!isHoistableInstruction(inst)) {
		return false;
	}

	for (int32_t index = 0; index < inst->getOperandsNum(); ++index) {
		if (!isLoopInvariantOperand(inst->getOperand(index), loop, cfg, dominance, hoisted)) {
			return false;
		}
	}
	return true;
}

void moveBeforeTerminator(BasicBlock * block, Instruction * inst)
{
	auto & instructions = block->getInstructions();
	auto insertPos = instructions.end();
	if (!instructions.empty() && instructions.back()->isTerminator()) {
		insertPos = instructions.end() - 1;
	}
	instructions.insert(insertPos, inst);
}

bool runOnLoop(Function * function, const IRLoopInfo & loop)
{
	bool changed = false;
	bool localChanged = true;
	InstSet hoisted;

	while (localChanged) {
		localChanged = false;
		IRCFG cfg = IRCFGBuilder::build(function);
		DominanceInfo dominance = DominanceBuilder::build(cfg, function->getEntryBlock());
		for (auto * block: function->getBasicBlocks()) {
			if (loop.blocks.find(block) == loop.blocks.end()) {
				continue;
			}

			auto & instructions = block->getInstructions();
			for (auto iter = instructions.begin(); iter != instructions.end();) {
				auto * inst = *iter;
				if (!isLoopInvariantInstruction(function, inst, loop, cfg, dominance, hoisted)) {
					++iter;
					continue;
				}

				iter = instructions.erase(iter);
				moveBeforeTerminator(loop.preheader, inst);
				hoisted.insert(inst);
				localChanged = true;
				changed = true;
			}
		}
	}

	return changed;
}

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	bool changed = false;
	bool localChanged = true;
	while (localChanged) {
		localChanged = false;
		for (const auto & loop: findNaturalLoops(function, true)) {
			if (runOnLoop(function, loop)) {
				localChanged = true;
				changed = true;
				break;
			}
		}
	}
	return changed;
}

} // namespace

bool LICMPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
