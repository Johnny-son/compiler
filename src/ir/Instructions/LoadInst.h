// LLVM load指令

#pragma once

#include "Instruction.h"

class LoadInst : public Instruction {
public:
	LoadInst(Function * func, Value * ptr, const std::string & name = "");

	Value * getPointerOperand() const;
	void toString(std::string & str) override;
};
