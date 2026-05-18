// Conservative loop canonicalization.

#include "LoopCanonicalizePass.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "Function.h"
#include "IRCFG.h"
#include "Instruction.h"
#include "LoopAnalysis.h"
#include "Module.h"
#include "PhiInst.h"
#include "Value.h"

namespace {

bool branchTargets(BranchInst * branch, BasicBlock * target)
{
	if (branch == nullptr || target == nullptr) {
		return false;
	}
	if (!branch->isConditional()) {
		return branch->getTarget() == target;
	}
	return branch->getTrueTarget() == target || branch->getFalseTarget() == target;
}

bool canRedirectToPreheader(BasicBlock * pred, BasicBlock * header)
{
	return pred != nullptr && branchTargets(dynamic_cast<BranchInst *>(pred->getTerminator()), header);
}

bool replaceBranchTarget(BasicBlock * block, BasicBlock * oldTarget, BasicBlock * newTarget)
{
	if (block == nullptr || oldTarget == nullptr || newTarget == nullptr) {
		return false;
	}

	auto & instructions = block->getInstructions();
	if (instructions.empty()) {
		return false;
	}

	auto * branch = dynamic_cast<BranchInst *>(instructions.back());
	if (branch == nullptr || !branchTargets(branch, oldTarget)) {
		return false;
	}

	BranchInst * replacement = nullptr;
	if (!branch->isConditional()) {
		replacement = new BranchInst(block->getParent(), newTarget);
	} else {
		auto * trueTarget = branch->getTrueTarget() == oldTarget ? newTarget : branch->getTrueTarget();
		auto * falseTarget = branch->getFalseTarget() == oldTarget ? newTarget : branch->getFalseTarget();
		replacement = new BranchInst(block->getParent(), branch->getOperand(0), trueTarget, falseTarget);
	}

	instructions.back() = replacement;
	branch->clearOperands();
	branch->removeUses();
	delete branch;
	return true;
}

bool isPhi(Instruction * inst)
{
	return dynamic_cast<PhiInst *>(inst) != nullptr;
}

std::vector<PhiInst *> headerPhis(BasicBlock * header)
{
	std::vector<PhiInst *> phis;
	for (auto * inst: header->getInstructions()) {
		auto * phi = dynamic_cast<PhiInst *>(inst);
		if (phi == nullptr) {
			break;
		}
		phis.push_back(phi);
	}
	return phis;
}

bool containsBlock(const std::vector<BasicBlock *> & blocks, BasicBlock * block)
{
	return std::find(blocks.begin(), blocks.end(), block) != blocks.end();
}

void moveBlockBefore(Function * function, BasicBlock * block, BasicBlock * before)
{
	auto & blocks = function->getBasicBlocks();
	auto blockIter = std::find(blocks.begin(), blocks.end(), block);
	auto beforeIter = std::find(blocks.begin(), blocks.end(), before);
	if (blockIter == blocks.end() || beforeIter == blocks.end() || blockIter == beforeIter) {
		return;
	}

	auto * value = *blockIter;
	blocks.erase(blockIter);
	beforeIter = std::find(blocks.begin(), blocks.end(), before);
	blocks.insert(beforeIter, value);
}

void rewriteHeaderPhis(Function * function, const std::vector<BasicBlock *> & externalPreds, BasicBlock * header, BasicBlock * preheader)
{
	for (auto * phi: headerPhis(header)) {
		std::vector<std::pair<Value *, BasicBlock *>> forwarded;
		for (auto incoming: phi->getIncomingValues()) {
			if (containsBlock(externalPreds, incoming.second)) {
				forwarded.push_back(incoming);
			}
		}
		if (forwarded.empty()) {
			continue;
		}

		auto * preheaderPhi =
			new PhiInst(function, phi->getType(), function->allocateLocalName("loop.preheader.phi"));
		for (auto incoming: forwarded) {
			preheaderPhi->addIncoming(incoming.first, incoming.second);
		}

		auto & preheaderInstructions = preheader->getInstructions();
		auto insertPos = preheaderInstructions.begin();
		while (insertPos != preheaderInstructions.end() && isPhi(*insertPos)) {
			++insertPos;
		}
		preheaderInstructions.insert(insertPos, preheaderPhi);

		for (auto * pred: externalPreds) {
			(void) phi->removeIncomingFrom(pred);
		}
		phi->addIncoming(preheaderPhi, preheader);
	}
}

bool canonicalizeOneLoop(Function * function)
{
	IRCFG cfg = IRCFGBuilder::build(function);
	for (const auto & loop: findNaturalLoops(function, false)) {
		if (hasDedicatedPreheader(cfg, loop)) {
			continue;
		}
		if (!loopHasSingleEntry(cfg, loop)) {
			continue;
		}

		auto externalPreds = externalPredecessors(cfg, loop);
		if (externalPreds.empty()) {
			continue;
		}
		if (!std::all_of(externalPreds.begin(), externalPreds.end(), [&](BasicBlock * pred) {
				return canRedirectToPreheader(pred, loop.header);
			})) {
			continue;
		}

		auto * preheader = function->createBlock(loop.header->getIRName() + ".preheader");
		moveBlockBefore(function, preheader, loop.header);
		rewriteHeaderPhis(function, externalPreds, loop.header, preheader);
		preheader->appendInst(new BranchInst(function, loop.header));

		for (auto * pred: externalPreds) {
			if (!replaceBranchTarget(pred, loop.header, preheader)) {
				return false;
			}
		}
		return true;
	}
	return false;
}

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	bool changed = false;
	bool localChanged = true;
	while (localChanged) {
		localChanged = canonicalizeOneLoop(function);
		changed |= localChanged;
	}
	return changed;
}

} // namespace

bool LoopCanonicalizePass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
