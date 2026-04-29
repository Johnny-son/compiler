// LLVM zext指令

#include "ZExtInst.h"

ZExtInst::ZExtInst(Function * func, Value * value, Type * targetType, const std::string & name)
	: Instruction(func, IRInstOperator::IRINST_OP_MAX, targetType)
{
	this->IRName = name;
	addOperand(value);
}

Value * ZExtInst::getSourceValue() const
{
	return const_cast<ZExtInst *>(this)->getOperand(0);
}

void ZExtInst::toString(std::string & str)
{
	Value * value = getSourceValue();
	str = getIRName() + " = zext " + value->getType()->toString() + " " + value->getIRName() + " to " +
		  getType()->toString();
}
