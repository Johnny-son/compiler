// Control-flow analysis helpers for LLVM-style IR basic blocks.

#include "IRCFG.h"

#include <algorithm>
#include <deque>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "Function.h"
#include "Instruction.h"

namespace {

void addUnique(std::vector<BasicBlock *> & values, BasicBlock * value)
{
	if (value == nullptr) {
		return;
	}
	if (std::find(values.begin(), values.end(), value) == values.end()) {
		values.push_back(value);
	}
}

std::unordered_set<BasicBlock *> makeSet(const std::vector<BasicBlock *> & blocks)
{
	return std::unordered_set<BasicBlock *>{blocks.begin(), blocks.end()};
}

std::unordered_set<BasicBlock *> intersectSets(
	const std::unordered_set<BasicBlock *> & lhs,
	const std::unordered_set<BasicBlock *> & rhs)
{
	std::unordered_set<BasicBlock *> result;
	for (auto * value: lhs) {
		if (rhs.find(value) != rhs.end()) {
			result.insert(value);
		}
	}
	return result;
}

bool sameSet(const std::unordered_set<BasicBlock *> & lhs, const std::unordered_set<BasicBlock *> & rhs)
{
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (auto * value: lhs) {
		if (rhs.find(value) == rhs.end()) {
			return false;
		}
	}
	return true;
}

} // namespace

IRCFG IRCFGBuilder::build(Function * function)
{
	IRCFG cfg;
	if (function == nullptr) {
		return cfg;
	}

	cfg.blocks = function->getBasicBlocks();
	for (auto * block: cfg.blocks) {
		cfg.successors[block];
		cfg.predecessors[block];
		for (auto * inst: block->getInstructions()) {
			cfg.instructionBlock[inst] = block;
		}

		auto * branch = dynamic_cast<BranchInst *>(block->getTerminator());
		if (branch == nullptr) {
			continue;
		}

		if (branch->isConditional()) {
			addUnique(cfg.successors[block], branch->getTrueTarget());
			addUnique(cfg.successors[block], branch->getFalseTarget());
		} else {
			addUnique(cfg.successors[block], branch->getTarget());
		}
	}

	for (auto * block: cfg.blocks) {
		for (auto * succ: cfg.successors[block]) {
			addUnique(cfg.predecessors[succ], block);
		}
	}

	BasicBlock * entry = function->getEntryBlock();
	if (entry != nullptr) {
		std::deque<BasicBlock *> worklist;
		worklist.push_back(entry);
		cfg.reachable.insert(entry);
		while (!worklist.empty()) {
			auto * block = worklist.front();
			worklist.pop_front();
			for (auto * succ: cfg.successors[block]) {
				if (cfg.reachable.insert(succ).second) {
					worklist.push_back(succ);
				}
			}
		}
	}

	return cfg;
}

bool DominanceInfo::dominates(BasicBlock * dominator, BasicBlock * block) const
{
	auto iter = dominators.find(block);
	return iter != dominators.end() && iter->second.find(dominator) != iter->second.end();
}

DominanceInfo DominanceBuilder::build(const IRCFG & cfg, BasicBlock * entry)
{
	DominanceInfo info;
	if (entry == nullptr || cfg.reachable.find(entry) == cfg.reachable.end()) {
		return info;
	}

	std::vector<BasicBlock *> reachableBlocks;
	for (auto * block: cfg.blocks) {
		if (cfg.reachable.find(block) != cfg.reachable.end()) {
			reachableBlocks.push_back(block);
		}
	}

	const auto allReachable = makeSet(reachableBlocks);
	for (auto * block: reachableBlocks) {
		if (block == entry) {
			info.dominators[block] = {block};
		} else {
			info.dominators[block] = allReachable;
		}
	}

	bool changed = true;
	while (changed) {
		changed = false;
		for (auto * block: reachableBlocks) {
			if (block == entry) {
				continue;
			}

			bool foundPred = false;
			std::unordered_set<BasicBlock *> newDominators;
			for (auto * pred: cfg.predecessors.at(block)) {
				if (cfg.reachable.find(pred) == cfg.reachable.end()) {
					continue;
				}
				if (!foundPred) {
					newDominators = info.dominators[pred];
					foundPred = true;
				} else {
					newDominators = intersectSets(newDominators, info.dominators[pred]);
				}
			}

			if (!foundPred) {
				newDominators.clear();
			}
			newDominators.insert(block);

			if (!sameSet(newDominators, info.dominators[block])) {
				info.dominators[block] = std::move(newDominators);
				changed = true;
			}
		}
	}

	info.immediateDominator[entry] = nullptr;
	for (auto * block: reachableBlocks) {
		if (block == entry) {
			continue;
		}

		std::vector<BasicBlock *> strictDominators;
		for (auto * dominator: info.dominators[block]) {
			if (dominator != block) {
				strictDominators.push_back(dominator);
			}
		}

		BasicBlock * idom = nullptr;
		for (auto * candidate: strictDominators) {
			bool deepest = true;
			for (auto * other: strictDominators) {
				if (other == candidate) {
					continue;
				}
				if (!info.dominates(other, candidate)) {
					deepest = false;
					break;
				}
			}
			if (deepest) {
				idom = candidate;
				break;
			}
		}

		info.immediateDominator[block] = idom;
		if (idom != nullptr) {
			info.dominatorTreeChildren[idom].push_back(block);
		}
	}

	for (auto * block: reachableBlocks) {
		info.dominanceFrontier[block];
	}

	for (auto * block: reachableBlocks) {
		std::vector<BasicBlock *> reachablePreds;
		for (auto * pred: cfg.predecessors.at(block)) {
			if (cfg.reachable.find(pred) != cfg.reachable.end()) {
				reachablePreds.push_back(pred);
			}
		}

		if (reachablePreds.size() < 2) {
			continue;
		}

		for (auto * pred: reachablePreds) {
			auto * runner = pred;
			while (runner != nullptr && runner != info.immediateDominator[block]) {
				info.dominanceFrontier[runner].insert(block);
				runner = info.immediateDominator[runner];
			}
		}
	}

	return info;
}
