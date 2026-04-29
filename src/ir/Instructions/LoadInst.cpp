// LLVM load指令

#include "LoadInst.h"

#include "ir/Types/PointerType.h"
#include "ir/Values/GlobalVariable.h"

namespace {

Type * getPointeeType(Value * ptr)
{
	if (dynamic_cast<GlobalVariable *>(ptr) != nullptr) {
		return ptr->getType();
	}
	auto * ptrType = dynamic_cast<PointerType *>(ptr->getType());
	if (ptrType == nullptr) {
		return ptr->getType();
	}
	return const_cast<Type *>(ptrType->getPointeeType());
}

} // namespace

LoadInst::LoadInst(Function * func, Value * ptr, const std::string & name)
	: Instruction(func, getPointeeType(ptr))
{
	this->IRName = name;
	addOperand(ptr);
}

Value * LoadInst::getPointerOperand() const
{
	return const_cast<LoadInst *>(this)->getOperand(0);
}

void LoadInst::toString(std::string & str)
{
	Value * ptr = getPointerOperand();
	std::string ptrType = ptr->getType()->toString();
	if (dynamic_cast<GlobalVariable *>(ptr) != nullptr) {
		ptrType += "*";
	}
	str = getIRName() + " = load " + getType()->toString() + ", " + ptrType + " " + ptr->getIRName();
}
