// LLVM icmp指令

#include "ICmpInst.h"

#include "ir/Types/IntegerType.h"

namespace {

const char * predicateName(ICmpInst::Predicate predicate)
{
	switch (predicate) {
		case ICmpInst::Predicate::EQ:
			return "eq";
		case ICmpInst::Predicate::NE:
			return "ne";
		case ICmpInst::Predicate::SLT:
			return "slt";
		case ICmpInst::Predicate::SLE:
			return "sle";
		case ICmpInst::Predicate::SGT:
			return "sgt";
		case ICmpInst::Predicate::SGE:
			return "sge";
	}
	return "eq";
}

} // namespace

ICmpInst::ICmpInst(Function * func, Predicate predicate, Value * lhs, Value * rhs, const std::string & name)
	: Instruction(func, IRInstOperator::IRINST_OP_MAX, IntegerType::getTypeBool()), predicate(predicate)
{
	this->IRName = name;
	addOperand(lhs);
	addOperand(rhs);
}

ICmpInst::Predicate ICmpInst::getPredicate() const
{
	return predicate;
}

void ICmpInst::toString(std::string & str)
{
	Value * lhs = getOperand(0);
	Value * rhs = getOperand(1);
	str = getIRName() + " = icmp " + predicateName(predicate) + " " + lhs->getType()->toString() + " " +
		  lhs->getIRName() + ", " + rhs->getIRName();
}
