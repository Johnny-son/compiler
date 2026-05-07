// Control-flow analysis helpers for LLVM-style IR basic blocks.

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

class BasicBlock;
class Function;
class Instruction;

struct IRCFG {
	std::vector<BasicBlock *> blocks;
	std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> successors;
	std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> predecessors;
	std::unordered_map<Instruction *, BasicBlock *> instructionBlock;
	std::unordered_set<BasicBlock *> reachable;
};

class IRCFGBuilder {
public:
	static IRCFG build(Function * function);
};

class DominanceInfo {
public:
	std::unordered_map<BasicBlock *, std::unordered_set<BasicBlock *>> dominators;
	std::unordered_map<BasicBlock *, BasicBlock *> immediateDominator;
	std::unordered_map<BasicBlock *, std::unordered_set<BasicBlock *>> dominanceFrontier;
	std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> dominatorTreeChildren;

	bool dominates(BasicBlock * dominator, BasicBlock * block) const;
};

class DominanceBuilder {
public:
	static DominanceInfo build(const IRCFG & cfg, BasicBlock * entry);
};
