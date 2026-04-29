// LLVM store指令

#pragma once

#include "Instruction.h"

class StoreInst : public Instruction {
public:
	StoreInst(Function * func, Value * value, Value * ptr);

	Value * getValueOperand() const;
	Value * getPointerOperand() const;
	void toString(std::string & str) override;
};
