// LLVM br指令

#pragma once

#include "Instruction.h"

class BasicBlock;

class BranchInst : public Instruction {
public:
	BranchInst(Function * func, BasicBlock * target);
	BranchInst(Function * func, Value * cond, BasicBlock * trueTarget, BasicBlock * falseTarget);

	bool isConditional() const;
	BasicBlock * getTrueTarget() const;
	BasicBlock * getFalseTarget() const;
	BasicBlock * getTarget() const;
	bool isTerminator() const override;
	void toString(std::string & str) override;

private:
	BasicBlock * trueTarget = nullptr;
	BasicBlock * falseTarget = nullptr;
};
