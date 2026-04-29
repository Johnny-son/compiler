// LLVM ret指令

#pragma once

#include "Instruction.h"

class ReturnInst : public Instruction {
public:
	explicit ReturnInst(Function * func);
	ReturnInst(Function * func, Value * value);

	bool isTerminator() const override;
	void toString(std::string & str) override;
};
