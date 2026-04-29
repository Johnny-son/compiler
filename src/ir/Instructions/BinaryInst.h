// LLVM二元运算指令

#pragma once

#include "Instruction.h"

class BinaryInst : public Instruction {
public:
	enum class Op {
		Add,
		Sub,
		Mul,
		SDiv,
		SRem,
		FAdd,
		FSub,
		FMul,
		FDiv,
	};

	BinaryInst(Function * func, Op op, Value * lhs, Value * rhs, const std::string & name = "");

	Op getBinaryOp() const;
	void toString(std::string & str) override;

private:
	Op binaryOp;
};
