// LLVM fcmp指令

#include "FCmpInst.h"

#include "ir/Types/IntegerType.h"

namespace {

const char * predicateName(FCmpInst::Predicate predicate)
{
	switch (predicate) {
		case FCmpInst::Predicate::OEQ:
			return "oeq";
		case FCmpInst::Predicate::ONE:
			return "one";
		case FCmpInst::Predicate::OLT:
			return "olt";
		case FCmpInst::Predicate::OLE:
			return "ole";
		case FCmpInst::Predicate::OGT:
			return "ogt";
		case FCmpInst::Predicate::OGE:
			return "oge";
	}
	return "oeq";
}

} // namespace

FCmpInst::FCmpInst(Function * func, Predicate predicate, Value * lhs, Value * rhs, const std::string & name)
	: Instruction(func, IntegerType::getTypeBool()), predicate(predicate)
{
	this->IRName = name;
	addOperand(lhs);
	addOperand(rhs);
}

FCmpInst::Predicate FCmpInst::getPredicate() const
{
	return predicate;
}

void FCmpInst::toString(std::string & str)
{
	Value * lhs = getOperand(0);
	Value * rhs = getOperand(1);
	str = getIRName() + " = fcmp " + predicateName(predicate) + " " + lhs->getType()->toString() + " " +
		  lhs->getIRName() + ", " + rhs->getIRName();
}
