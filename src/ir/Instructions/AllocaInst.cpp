// LLVM alloca指令

#include "AllocaInst.h"

#include "ir/Types/PointerType.h"

AllocaInst::AllocaInst(Function * func, Type * allocatedType, const std::string & name)
	: Instruction(func, IRInstOperator::IRINST_OP_MAX, const_cast<PointerType *>(PointerType::get(allocatedType))),
	  allocatedType(allocatedType)
{
	this->IRName = name;
}

Type * AllocaInst::getAllocatedType() const
{
	return allocatedType;
}

void AllocaInst::toString(std::string & str)
{
	str = getIRName() + " = alloca " + allocatedType->toString() + ", align 4";
}
