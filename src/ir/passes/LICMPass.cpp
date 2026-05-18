// Conservative loop-invariant code motion.

#include "LICMPass.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CastInst.h"
#include "FCmpInst.h"
#include "GetElementPtrInst.h"
#include "IRCFG.h"
#include "ICmpInst.h"
#include "ZExtInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"

namespace {

using BlockSet = std::unordered_set<BasicBlock *>;
using InstSet = std::unordered_set<Instruction *>;

struct LoopInfo {
	BasicBlock * header = nullptr;
	BasicBlock * latch = nullptr;
	BasicBlock * preheader = nullptr;
	BlockSet blocks;
};

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

bool isLoopInvariantOperand(
	Value * value,
	const LoopInfo & loop,
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
	Instruction * inst,
	const LoopInfo & loop,
	const IRCFG & cfg,
	const DominanceInfo & dominance,
	const InstSet & hoisted)
{
	if (!isHoistableInstruction(inst)) {
		return false;
	}

	for (int32_t index = 0; index < inst->getOperandsNum(); ++index) {
		if (!isLoopInvariantOperand(inst->getOperand(index), loop, cfg, dominance, hoisted)) {
			return false;
		}
	}
	return true;
}

bool hasUnconditionalBranchTo(BasicBlock * block, BasicBlock * target)
{
	if (block == nullptr || target == nullptr) {
		return false;
	}
	auto * branch = dynamic_cast<BranchInst *>(block->getTerminator());
	return branch != nullptr && !branch->isConditional() && branch->getTarget() == target;
}

bool findPreheader(const IRCFG & cfg, LoopInfo & loop)
{
	auto predIter = cfg.predecessors.find(loop.header);
	if (predIter == cfg.predecessors.end()) {
		return false;
	}

	BasicBlock * preheader = nullptr;
	for (auto * pred: predIter->second) {
		if (loop.blocks.find(pred) != loop.blocks.end()) {
			continue;
		}
		if (preheader != nullptr) {
			return false;
		}
		preheader = pred;
	}

	if (preheader == nullptr || !hasUnconditionalBranchTo(preheader, loop.header)) {
		return false;
	}
	loop.preheader = preheader;
	return true;
}

bool hasSingleEntry(const IRCFG & cfg, const DominanceInfo & dominance, const LoopInfo & loop)
{
	for (auto * block: loop.blocks) {
		if (!dominance.dominates(loop.header, block)) {
			return false;
		}

		auto predIter = cfg.predecessors.find(block);
		if (predIter == cfg.predecessors.end()) {
			continue;
		}
		for (auto * pred: predIter->second) {
			if (loop.blocks.find(pred) != loop.blocks.end()) {
				continue;
			}
			if (block != loop.header || pred != loop.preheader) {
				return false;
			}
		}
	}
	return true;
}

LoopInfo buildNaturalLoop(const IRCFG & cfg, BasicBlock * header, BasicBlock * latch)
{
	LoopInfo loop;
	loop.header = header;
	loop.latch = latch;
	loop.blocks.insert(header);
	loop.blocks.insert(latch);

	std::vector<BasicBlock *> worklist{latch};
	while (!worklist.empty()) {
		auto * block = worklist.back();
		worklist.pop_back();

		auto predIter = cfg.predecessors.find(block);
		if (predIter == cfg.predecessors.end()) {
			continue;
		}
		for (auto * pred: predIter->second) {
			if (loop.blocks.insert(pred).second) {
				worklist.push_back(pred);
			}
		}
	}

	return loop;
}

std::vector<LoopInfo> findLoops(Function * function)
{
	std::vector<LoopInfo> loops;
	IRCFG cfg = IRCFGBuilder::build(function);
	DominanceInfo dominance = DominanceBuilder::build(cfg, function->getEntryBlock());

	for (auto * latch: cfg.blocks) {
		if (cfg.reachable.find(latch) == cfg.reachable.end()) {
			continue;
		}

		auto succIter = cfg.successors.find(latch);
		if (succIter == cfg.successors.end()) {
			continue;
		}
		for (auto * header: succIter->second) {
			if (cfg.reachable.find(header) == cfg.reachable.end() || !dominance.dominates(header, latch)) {
				continue;
			}

			LoopInfo loop = buildNaturalLoop(cfg, header, latch);
			if (findPreheader(cfg, loop) && hasSingleEntry(cfg, dominance, loop)) {
				loops.push_back(std::move(loop));
			}
		}
	}

	std::sort(loops.begin(), loops.end(), [](const LoopInfo & lhs, const LoopInfo & rhs) {
		return lhs.blocks.size() < rhs.blocks.size();
	});
	return loops;
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

bool runOnLoop(Function * function, const LoopInfo & loop)
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
				if (!isLoopInvariantInstruction(inst, loop, cfg, dominance, hoisted)) {
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
		for (const auto & loop: findLoops(function)) {
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
