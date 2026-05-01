// LLVM fcmp指令

#pragma once

#include "Instruction.h"

class FCmpInst : public Instruction {
public:
	enum class Predicate {
		OEQ,
		ONE,
		OLT,
		OLE,
		OGT,
		OGE,
	};

	FCmpInst(Function * func, Predicate predicate, Value * lhs, Value * rhs, const std::string & name = "");

	Predicate getPredicate() const;
	void toString(std::string & str) override;

private:
	Predicate predicate;
};
