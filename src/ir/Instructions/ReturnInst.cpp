// LLVM ret指令

#include "ReturnInst.h"

#include "ir/Types/VoidType.h"

ReturnInst::ReturnInst(Function * func) : Instruction(func, IRInstOperator::IRINST_OP_MAX, VoidType::getType())
{}

ReturnInst::ReturnInst(Function * func, Value * value) : Instruction(func, IRInstOperator::IRINST_OP_MAX, VoidType::getType())
{
	addOperand(value);
}

bool ReturnInst::isTerminator() const
{
	return true;
}

void ReturnInst::toString(std::string & str)
{
	if (getOperandsNum() == 0) {
		str = "ret void";
		return;
	}

	Value * value = getOperand(0);
	str = "ret " + value->getType()->toString() + " " + value->getIRName();
}
