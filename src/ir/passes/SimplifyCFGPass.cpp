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
#include "ICmpInst.h"
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

Value * phiValueFrom(PhiInst * phi, BasicBlock * block)
{
	if (phi == nullptr || block == nullptr) {
		return nullptr;
	}

	for (const auto & incoming: phi->getIncomingValues()) {
		if (incoming.second == block) {
			return incoming.first;
		}
	}
	return nullptr;
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

bool branchHasDoubleEdgeTo(BranchInst * branch, BasicBlock * target)
{
	return branch != nullptr && branch->isConditional() && branch->getTrueTarget() == target &&
		   branch->getFalseTarget() == target;
}

bool evaluateICmp(ICmpInst::Predicate predicate, int32_t lhs, int32_t rhs)
{
	switch (predicate) {
		case ICmpInst::Predicate::EQ:
			return lhs == rhs;
		case ICmpInst::Predicate::NE:
			return lhs != rhs;
		case ICmpInst::Predicate::SLT:
			return lhs < rhs;
		case ICmpInst::Predicate::SLE:
			return lhs <= rhs;
		case ICmpInst::Predicate::SGT:
			return lhs > rhs;
		case ICmpInst::Predicate::SGE:
			return lhs >= rhs;
	}
	return false;
}

bool conditionForPredecessor(Value * condition, BasicBlock * pred, bool & takeTrueEdge)
{
	if (auto * phi = dynamic_cast<PhiInst *>(condition); phi != nullptr) {
		auto * incoming = dynamic_cast<ConstInt *>(phiValueFrom(phi, pred));
		if (incoming == nullptr) {
			return false;
		}
		takeTrueEdge = incoming->getVal() != 0;
		return true;
	}

	auto * icmp = dynamic_cast<ICmpInst *>(condition);
	if (icmp == nullptr) {
		return false;
	}

	auto * lhsPhi = dynamic_cast<PhiInst *>(icmp->getOperand(0));
	auto * rhsPhi = dynamic_cast<PhiInst *>(icmp->getOperand(1));
	auto * lhsConst = dynamic_cast<ConstInt *>(icmp->getOperand(0));
	auto * rhsConst = dynamic_cast<ConstInt *>(icmp->getOperand(1));

	if (lhsPhi != nullptr && rhsConst != nullptr) {
		auto * incoming = dynamic_cast<ConstInt *>(phiValueFrom(lhsPhi, pred));
		if (incoming == nullptr) {
			return false;
		}
		takeTrueEdge = evaluateICmp(icmp->getPredicate(), incoming->getVal(), rhsConst->getVal());
		return true;
	}

	if (lhsConst != nullptr && rhsPhi != nullptr) {
		auto * incoming = dynamic_cast<ConstInt *>(phiValueFrom(rhsPhi, pred));
		if (incoming == nullptr) {
			return false;
		}
		takeTrueEdge = evaluateICmp(icmp->getPredicate(), lhsConst->getVal(), incoming->getVal());
		return true;
	}

	return false;
}

bool isPhiBranchOnlyBlock(BasicBlock * block, Value * condition)
{
	if (block == nullptr || condition == nullptr) {
		return false;
	}

	for (auto * inst: block->getInstructions()) {
		if (dynamic_cast<PhiInst *>(inst) != nullptr) {
			continue;
		}
		if (inst == condition) {
			continue;
		}
		if (dynamic_cast<BranchInst *>(inst) != nullptr) {
			continue;
		}
		return false;
	}
	return true;
}

bool blockValuesStayLocal(BasicBlock * block, const IRCFG & cfg)
{
	if (block == nullptr) {
		return false;
	}

	for (auto * inst: block->getInstructions()) {
		if (inst == nullptr) {
			continue;
		}
		for (auto * use: inst->getUseList()) {
			auto * userInst = dynamic_cast<Instruction *>(use->getUser());
			if (userInst == nullptr) {
				return false;
			}
			auto userBlock = cfg.instructionBlock.find(userInst);
			if (userBlock == cfg.instructionBlock.end() || userBlock->second != block) {
				return false;
			}
		}
	}
	return true;
}

bool foldConstantPhiBranches(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	for (auto * block: function->getBasicBlocks()) {
		if (block == nullptr || block == function->getEntryBlock()) {
			continue;
		}
		if (cfg.reachable.find(block) == cfg.reachable.end() || !startsWithPhi(block)) {
			continue;
		}

		auto * branch = dynamic_cast<BranchInst *>(block->getTerminator());
		if (branch == nullptr || !branch->isConditional() || branch->getOperandsNum() != 1) {
			continue;
		}
		if (startsWithPhi(branch->getTrueTarget()) || startsWithPhi(branch->getFalseTarget())) {
			continue;
		}

		auto * condition = branch->getOperand(0);
		if (!isPhiBranchOnlyBlock(block, condition)) {
			continue;
		}
		if (!blockValuesStayLocal(block, cfg)) {
			continue;
		}

		auto predIter = cfg.predecessors.find(block);
		if (predIter == cfg.predecessors.end() || predIter->second.empty()) {
			continue;
		}

		std::vector<std::pair<BasicBlock *, BasicBlock *>> redirects;
		bool canFold = true;
		for (auto * pred: predIter->second) {
			if (cfg.reachable.find(pred) == cfg.reachable.end()) {
				continue;
			}
			auto * predBranch = dynamic_cast<BranchInst *>(pred->getTerminator());
			if (branchHasDoubleEdgeTo(predBranch, block)) {
				canFold = false;
				break;
			}

			bool takeTrueEdge = false;
			if (!conditionForPredecessor(condition, pred, takeTrueEdge)) {
				canFold = false;
				break;
			}
			redirects.emplace_back(pred, takeTrueEdge ? branch->getTrueTarget() : branch->getFalseTarget());
		}

		if (!canFold || redirects.empty()) {
			continue;
		}

		for (const auto & redirect: redirects) {
			replaceBranchTarget(redirect.first, block, redirect.second);
		}
		return true;
	}

	return false;
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
			changed |= foldConstantPhiBranches(function);
			changed |= removeUnreachableBlocks(function);
			changed |= bypassEmptyJumpBlock(function);
		}
	}
	return true;
}
