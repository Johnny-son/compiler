// LLVM getelementptr指令

#pragma once

#include <vector>

#include "Instruction.h"

class GetElementPtrInst : public Instruction {
public:
	GetElementPtrInst(Function * func, Value * basePtr, const std::vector<Value *> & indices, const std::string & name = "");

	Value * getBasePointer() const;
	const std::vector<Value *> getIndices() const;
	void toString(std::string & str) override;
};
