// LLVM store指令

#include "StoreInst.h"

#include "ir/Types/VoidType.h"
#include "ir/Values/GlobalVariable.h"

StoreInst::StoreInst(Function * func, Value * value, Value * ptr)
	: Instruction(func, IRInstOperator::IRINST_OP_MAX, VoidType::getType())
{
	addOperand(value);
	addOperand(ptr);
}

Value * StoreInst::getValueOperand() const
{
	return const_cast<StoreInst *>(this)->getOperand(0);
}

Value * StoreInst::getPointerOperand() const
{
	return const_cast<StoreInst *>(this)->getOperand(1);
}

void StoreInst::toString(std::string & str)
{
	Value * value = getValueOperand();
	Value * ptr = getPointerOperand();
	std::string ptrType = ptr->getType()->toString();
	if (dynamic_cast<GlobalVariable *>(ptr) != nullptr) {
		ptrType += "*";
	}
	str = "store " + value->getType()->toString() + " " + value->getIRName() + ", " + ptrType + " " + ptr->getIRName();
}
