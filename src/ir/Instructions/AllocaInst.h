// LLVM alloca指令

#pragma once

#include "Instruction.h"

class AllocaInst : public Instruction {
public:
	AllocaInst(Function * func, Type * allocatedType, const std::string & name = "");

	Type * getAllocatedType() const;
	void toString(std::string & str) override;

private:
	Type * allocatedType = nullptr;
};
