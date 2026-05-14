// Simplify control-flow graph structure.

#include "SimplifyCFGPass.h"

#include <algorithm>
#include <unordered_set>

#include "BasicBlock.h"
#include "IRCFG.h"
#include "BranchInst.h"
#include "PhiInst.h"
#include "ConstInt.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "User.h"

namespace {

using BlockSet = std::unordered_set<BasicBlock *>;
using InstSet = std::unordered_set<Instruction *>;

bool foldConstantBranches(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	bool changed = false;
	for (auto * block: function->getBasicBlocks()) {
		if (block == nullptr) {
			continue;
		}

		auto & instructions = block->getInstructions();
		if (instructions.empty()) {
			continue;
		}

		auto * branch = dynamic_cast<BranchInst *>(instructions.back());
		if (branch == nullptr || !branch->isConditional() || branch->getOperandsNum() != 1) {
			continue;
		}

		auto * cond = dynamic_cast<ConstInt *>(branch->getOperand(0));
		if (cond == nullptr) {
			continue;
		}

		const bool takeTrueEdge = cond->getVal() != 0;
		auto * target = takeTrueEdge ? branch->getTrueTarget() : branch->getFalseTarget();
		auto * removedTarget = takeTrueEdge ? branch->getFalseTarget() : branch->getTrueTarget();
		if (removedTarget != target) {
			for (auto * inst: removedTarget->getInstructions()) {
				auto * phi = dynamic_cast<PhiInst *>(inst);
				if (phi == nullptr) {
					break;
				}
				(void) phi->removeIncomingFrom(block);
			}
		}

		auto * foldedBranch = new BranchInst(function, target);
		instructions.back() = foldedBranch;
		branch->clearOperands();
		branch->removeUses();
		delete branch;
		changed = true;
	}

	return changed;
}

bool phiHasIncomingFrom(PhiInst * phi, BasicBlock * block)
{
	for (const auto & incoming: phi->getIncomingValues()) {
		if (incoming.second == block) {
			return true;
		}
	}
	return false;
}

bool startsWithPhi(BasicBlock * block)
{
	return block != nullptr && !block->getInstructions().empty() &&
		   dynamic_cast<PhiInst *>(block->getInstructions().front()) != nullptr;
}

bool canRedirectPhiIncoming(BasicBlock * target, BasicBlock * oldPred, BasicBlock * newPred)
{
	for (auto * inst: target->getInstructions()) {
		auto * phi = dynamic_cast<PhiInst *>(inst);
		if (phi == nullptr) {
			break;
		}
		if (phiHasIncomingFrom(phi, oldPred) && phiHasIncomingFrom(phi, newPred)) {
			return false;
		}
	}
	return true;
}

void replacePhiIncomingBlock(BasicBlock * target, BasicBlock * oldPred, BasicBlock * newPred)
{
	for (auto * inst: target->getInstructions()) {
		auto * phi = dynamic_cast<PhiInst *>(inst);
		if (phi == nullptr) {
			break;
		}
		phi->replaceIncomingBlock(oldPred, newPred);
	}
}

void replaceBranchTarget(BasicBlock * block, BasicBlock * oldTarget, BasicBlock * newTarget)
{
	if (block == nullptr || oldTarget == nullptr || newTarget == nullptr) {
		return;
	}

	auto & instructions = block->getInstructions();
	if (instructions.empty()) {
		return;
	}

	auto * branch = dynamic_cast<BranchInst *>(instructions.back());
	if (branch == nullptr) {
		return;
	}

	BranchInst * replacement = nullptr;
	if (!branch->isConditional()) {
		if (branch->getTarget() != oldTarget) {
			return;
		}
		replacement = new BranchInst(block->getParent(), newTarget);
	} else {
		auto * trueTarget = branch->getTrueTarget() == oldTarget ? newTarget : branch->getTrueTarget();
		auto * falseTarget = branch->getFalseTarget() == oldTarget ? newTarget : branch->getFalseTarget();
		if (trueTarget == branch->getTrueTarget() && falseTarget == branch->getFalseTarget()) {
			return;
		}
		replacement = new BranchInst(block->getParent(), branch->getOperand(0), trueTarget, falseTarget);
	}

	instructions.back() = replacement;
	branch->clearOperands();
	branch->removeUses();
	delete branch;
}

void collectInstructions(const BlockSet & blocks, InstSet & instructions)
{
	for (auto * block: blocks) {
		if (block == nullptr) {
			continue;
		}
		for (auto * inst: block->getInstructions()) {
			if (inst != nullptr) {
				instructions.insert(inst);
			}
		}
	}
}

void removePhiIncomingFromDeadBlocks(Function * function, const BlockSet & deadBlocks)
{
	for (auto * block: function->getBasicBlocks()) {
		if (block == nullptr || deadBlocks.find(block) != deadBlocks.end()) {
			continue;
		}

		for (auto * inst: block->getInstructions()) {
			auto * phi = dynamic_cast<PhiInst *>(inst);
			if (phi == nullptr) {
				break;
			}

			for (auto * deadBlock: deadBlocks) {
				(void) phi->removeIncomingFrom(deadBlock);
			}
		}
	}
}

bool isRemovableDeadPhiUse(Use * use, const BlockSet & deadBlocks)
{
	auto * phi = dynamic_cast<PhiInst *>(use->getUser());
	if (phi == nullptr) {
		return false;
	}

	const auto & incomingValues = phi->getIncomingValues();
	auto & operands = phi->getOperands();
	const std::size_t count = std::min(incomingValues.size(), operands.size());
	for (std::size_t index = 0; index < count; ++index) {
		if (operands[index] == use && deadBlocks.find(incomingValues[index].second) != deadBlocks.end()) {
			return true;
		}
	}
	return false;
}

bool hasUsesOutsideDeadBlocks(const InstSet & deadInstructions, const BlockSet & deadBlocks)
{
	for (auto * inst: deadInstructions) {
		for (auto * use: inst->getUseList()) {
			auto * userInst = dynamic_cast<Instruction *>(use->getUser());
			if (userInst != nullptr && deadInstructions.find(userInst) != deadInstructions.end()) {
				continue;
			}
			if (isRemovableDeadPhiUse(use, deadBlocks)) {
				continue;
			}
			return true;
		}
	}
	return false;
}

void deleteBlock(BasicBlock * block)
{
	if (block == nullptr) {
		return;
	}

	auto & instructions = block->getInstructions();
	while (!instructions.empty()) {
		auto * inst = instructions.back();
		instructions.pop_back();
		inst->clearOperands();
		inst->removeUses();
		delete inst;
	}
	delete block;
}

bool removeUnreachableBlocks(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	BlockSet deadBlocks;
	for (auto * block: cfg.blocks) {
		if (cfg.reachable.find(block) == cfg.reachable.end()) {
			deadBlocks.insert(block);
		}
	}

	if (deadBlocks.empty()) {
		return false;
	}

	InstSet deadInstructions;
	collectInstructions(deadBlocks, deadInstructions);
	if (hasUsesOutsideDeadBlocks(deadInstructions, deadBlocks)) {
		// A reachable instruction still references a dead-block value. Keep the
		// blocks rather than creating dangling operands; VerifyPass will surface it.
		return false;
	}

	removePhiIncomingFromDeadBlocks(function, deadBlocks);

	auto & blocks = function->getBasicBlocks();
	for (auto iter = blocks.begin(); iter != blocks.end();) {
		if (deadBlocks.find(*iter) == deadBlocks.end()) {
			++iter;
			continue;
		}

		auto * block = *iter;
		iter = blocks.erase(iter);
		deleteBlock(block);
	}

	return true;
}

bool bypassEmptyJumpBlock(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	for (auto * block: function->getBasicBlocks()) {
		if (block == nullptr || block == function->getEntryBlock()) {
			continue;
		}
		if (cfg.reachable.find(block) == cfg.reachable.end()) {
			continue;
		}
		if (block->getInstructions().size() != 1) {
			continue;
		}

		auto * branch = dynamic_cast<BranchInst *>(block->getTerminator());
		if (branch == nullptr || branch->isConditional()) {
			continue;
		}

		auto * target = branch->getTarget();
		if (target == nullptr || target == block) {
			continue;
		}
		if (startsWithPhi(target)) {
			continue;
		}

		auto predIter = cfg.predecessors.find(block);
		if (predIter == cfg.predecessors.end() || predIter->second.size() != 1) {
			continue;
		}

		auto * pred = predIter->second.front();
		if (pred == nullptr || !canRedirectPhiIncoming(target, block, pred)) {
			continue;
		}

		replaceBranchTarget(pred, block, target);
		replacePhiIncomingBlock(target, block, pred);

		auto & blocks = function->getBasicBlocks();
		auto iter = std::find(blocks.begin(), blocks.end(), block);
		if (iter != blocks.end()) {
			blocks.erase(iter);
		}
		deleteBlock(block);
		return true;
	}

	return false;
}

} // namespace

bool SimplifyCFGPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		bool changed = true;
		while (changed) {
			changed = false;
			changed |= foldConstantBranches(function);
			changed |= removeUnreachableBlocks(function);
			changed |= bypassEmptyJumpBlock(function);
		}
	}
	return true;
}
