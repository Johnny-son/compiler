// LLVM call指令

#include "CallInst.h"

#include "Function.h"

CallInst::CallInst(
	Function * func, Function * callee, const std::vector<Value *> & args, Type * returnType, const std::string & name)
	: Instruction(func, IRInstOperator::IRINST_OP_MAX, returnType), callee(callee)
{
	this->IRName = name;
	for (auto * arg: args) {
		addOperand(arg);
	}
}

Function * CallInst::getCallee() const
{
	return callee;
}

void CallInst::toString(std::string & str)
{
	if (hasResultValue()) {
		str = getIRName() + " = ";
	} else {
		str.clear();
	}

	str += "call " + callee->getReturnType()->toString() + " " + callee->getIRName() + "(";
	for (int32_t idx = 0; idx < getOperandsNum(); ++idx) {
		if (idx != 0) {
			str += ", ";
		}
		Value * arg = getOperand(idx);
		str += arg->getType()->toString() + " " + arg->getIRName();
	}
	str += ")";
}
