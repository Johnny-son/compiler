// Shared natural-loop analysis for loop optimization passes.

#include "LoopAnalysis.h"

#include <algorithm>
#include <unordered_map>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "Function.h"
#include "IRCFG.h"

namespace {

bool hasUnconditionalBranchTo(BasicBlock * block, BasicBlock * target)
{
	if (block == nullptr || target == nullptr) {
		return false;
	}
	auto * branch = dynamic_cast<BranchInst *>(block->getTerminator());
	return branch != nullptr && !branch->isConditional() && branch->getTarget() == target;
}

void addUnique(std::vector<BasicBlock *> & values, BasicBlock * value)
{
	if (value == nullptr || std::find(values.begin(), values.end(), value) != values.end()) {
		return;
	}
	values.push_back(value);
}

void addNaturalLoopBlocks(const IRCFG & cfg, IRLoopInfo & loop, BasicBlock * latch)
{
	loop.blocks.insert(loop.header);
	loop.blocks.insert(latch);

	std::vector<BasicBlock *> worklist{latch};
	while (!worklist.empty()) {
		auto * block = worklist.back();
		worklist.pop_back();
		if (block == loop.header) {
			continue;
		}

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
}

void fillLoopExits(const IRCFG & cfg, IRLoopInfo & loop)
{
	for (auto * block: loop.blocks) {
		auto succIter = cfg.successors.find(block);
		if (succIter == cfg.successors.end()) {
			continue;
		}
		for (auto * succ: succIter->second) {
			if (!loop.contains(succ)) {
				loop.exitingBlocks.insert(block);
				loop.exitBlocks.insert(succ);
			}
		}
	}
}

void fillPreheader(const IRCFG & cfg, IRLoopInfo & loop)
{
	auto external = externalPredecessors(cfg, loop);
	if (external.size() != 1) {
		return;
	}
	auto * candidate = external.front();
	auto succIter = cfg.successors.find(candidate);
	if (succIter == cfg.successors.end() || succIter->second.size() != 1) {
		return;
	}
	if (hasUnconditionalBranchTo(candidate, loop.header)) {
		loop.preheader = candidate;
	}
}

} // namespace

bool IRLoopInfo::contains(BasicBlock * block) const
{
	return block != nullptr && blocks.find(block) != blocks.end();
}

std::vector<BasicBlock *> externalPredecessors(const IRCFG & cfg, const IRLoopInfo & loop)
{
	std::vector<BasicBlock *> result;
	auto predIter = cfg.predecessors.find(loop.header);
	if (predIter == cfg.predecessors.end()) {
		return result;
	}
	for (auto * pred: predIter->second) {
		if (!loop.contains(pred)) {
			addUnique(result, pred);
		}
	}
	return result;
}

bool loopHasSingleEntry(const IRCFG & cfg, const IRLoopInfo & loop)
{
	for (auto * block: loop.blocks) {
		auto predIter = cfg.predecessors.find(block);
		if (predIter == cfg.predecessors.end()) {
			continue;
		}
		for (auto * pred: predIter->second) {
			if (loop.contains(pred)) {
				continue;
			}
			if (block != loop.header) {
				return false;
			}
		}
	}
	return true;
}

bool hasDedicatedPreheader(const IRCFG & cfg, const IRLoopInfo & loop)
{
	if (loop.preheader == nullptr) {
		return false;
	}
	auto succIter = cfg.successors.find(loop.preheader);
	return succIter != cfg.successors.end() && succIter->second.size() == 1 && succIter->second.front() == loop.header;
}

std::vector<IRLoopInfo> findNaturalLoops(Function * function, bool requirePreheader)
{
	std::vector<IRLoopInfo> loops;
	if (function == nullptr || function->getEntryBlock() == nullptr) {
		return loops;
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	DominanceInfo dominance = DominanceBuilder::build(cfg, function->getEntryBlock());
	std::unordered_map<BasicBlock *, std::size_t> loopIndexForHeader;

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

			auto iter = loopIndexForHeader.find(header);
			if (iter == loopIndexForHeader.end()) {
				IRLoopInfo loop;
				loop.header = header;
				loopIndexForHeader[header] = loops.size();
				loops.push_back(std::move(loop));
				iter = loopIndexForHeader.find(header);
			}

			auto & loop = loops[iter->second];
			addUnique(loop.latches, latch);
			addNaturalLoopBlocks(cfg, loop, latch);
		}
	}

	for (auto & loop: loops) {
		fillPreheader(cfg, loop);
		fillLoopExits(cfg, loop);
	}

	loops.erase(
		std::remove_if(
			loops.begin(),
			loops.end(),
			[&](const IRLoopInfo & loop) {
				return !loopHasSingleEntry(cfg, loop) || (requirePreheader && !hasDedicatedPreheader(cfg, loop));
			}),
		loops.end());

	std::sort(loops.begin(), loops.end(), [](const IRLoopInfo & lhs, const IRLoopInfo & rhs) {
		return lhs.blocks.size() < rhs.blocks.size();
	});
	return loops;
}
