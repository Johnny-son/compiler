// Shared natural-loop analysis for loop optimization passes.

#pragma once

#include <unordered_set>
#include <vector>

class BasicBlock;
class Function;
struct IRCFG;

struct IRLoopInfo {
	BasicBlock * header = nullptr;
	BasicBlock * preheader = nullptr;
	std::vector<BasicBlock *> latches;
	std::unordered_set<BasicBlock *> blocks;
	std::unordered_set<BasicBlock *> exitingBlocks;
	std::unordered_set<BasicBlock *> exitBlocks;

	bool contains(BasicBlock * block) const;
};

std::vector<BasicBlock *> externalPredecessors(const IRCFG & cfg, const IRLoopInfo & loop);
bool loopHasSingleEntry(const IRCFG & cfg, const IRLoopInfo & loop);
bool hasDedicatedPreheader(const IRCFG & cfg, const IRLoopInfo & loop);
std::vector<IRLoopInfo> findNaturalLoops(Function * function, bool requirePreheader = false);
