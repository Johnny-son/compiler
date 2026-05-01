// LLVM类型转换指令

#pragma once

#include "Instruction.h"

class CastInst : public Instruction {
public:
	enum class Op {
		SIToFP,
		FPToSI,
	};

	CastInst(Function * func, Op op, Value * value, Type * targetType, const std::string & name = "");

	Op getCastOp() const;
	void toString(std::string & str) override;

private:
	Op castOp;
};
