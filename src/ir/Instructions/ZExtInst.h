// LLVM zext指令

#pragma once

#include "Instruction.h"

class ZExtInst : public Instruction {
public:
	ZExtInst(Function * func, Value * value, Type * targetType, const std::string & name = "");

	Value * getSourceValue() const;
	void toString(std::string & str) override;
};
