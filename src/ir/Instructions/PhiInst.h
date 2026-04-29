// LLVM phi指令

#pragma once

#include <utility>
#include <vector>

#include "Instruction.h"

class BasicBlock;

class PhiInst : public Instruction {
public:
	PhiInst(Function * func, Type * type, const std::string & name = "");

	void addIncoming(Value * value, BasicBlock * block);
	const std::vector<std::pair<Value *, BasicBlock *>> & getIncomingValues() const;
	void toString(std::string & str) override;

private:
	std::vector<std::pair<Value *, BasicBlock *>> incomingValues;
};
