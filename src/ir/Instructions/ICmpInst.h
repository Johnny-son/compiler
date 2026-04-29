// LLVM icmp指令

#pragma once

#include "Instruction.h"

class ICmpInst : public Instruction {
public:
	enum class Predicate {
		EQ,
		NE,
		SLT,
		SLE,
		SGT,
		SGE,
	};

	ICmpInst(Function * func, Predicate predicate, Value * lhs, Value * rhs, const std::string & name = "");

	Predicate getPredicate() const;
	void toString(std::string & str) override;

private:
	Predicate predicate;
};
