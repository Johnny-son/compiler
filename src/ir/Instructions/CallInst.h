// LLVM call指令

#pragma once

#include <vector>

#include "Instruction.h"

class Function;

class CallInst : public Instruction {
public:
	CallInst(Function * func, Function * callee, const std::vector<Value *> & args, Type * returnType, const std::string & name = "");

	Function * getCallee() const;
	void toString(std::string & str) override;

private:
	Function * callee = nullptr;
};
