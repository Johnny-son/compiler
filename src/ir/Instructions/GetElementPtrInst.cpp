// LLVM getelementptr指令

#include "GetElementPtrInst.h"

#include "ir/Types/ArrayType.h"
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

Type * inferResultElementType(Value * basePtr, size_t indexCount)
{
	Type * current = getPointeeType(basePtr);
	for (size_t idx = 1; idx < indexCount; ++idx) {
		if (auto * arrayType = dynamic_cast<ArrayType *>(current); arrayType != nullptr) {
			current = arrayType->getElementType();
		}
	}
	return current;
}

} // namespace

GetElementPtrInst::GetElementPtrInst(
	Function * func, Value * basePtr, const std::vector<Value *> & indices, const std::string & name)
	: Instruction(
		  func,
		  const_cast<PointerType *>(PointerType::get(inferResultElementType(basePtr, indices.size()))))
{
	this->IRName = name;
	addOperand(basePtr);
	for (auto * index: indices) {
		addOperand(index);
	}
}

Value * GetElementPtrInst::getBasePointer() const
{
	return const_cast<GetElementPtrInst *>(this)->getOperand(0);
}

const std::vector<Value *> GetElementPtrInst::getIndices() const
{
	std::vector<Value *> indices;
	for (int32_t idx = 1; idx < const_cast<GetElementPtrInst *>(this)->getOperandsNum(); ++idx) {
		indices.push_back(const_cast<GetElementPtrInst *>(this)->getOperand(idx));
	}
	return indices;
}

void GetElementPtrInst::toString(std::string & str)
{
	Value * basePtr = getBasePointer();
	Type * sourceType = getPointeeType(basePtr);
	std::string basePtrType = basePtr->getType()->toString();
	if (dynamic_cast<GlobalVariable *>(basePtr) != nullptr) {
		basePtrType += "*";
	}
	str = getIRName() + " = getelementptr inbounds " + sourceType->toString() + ", " + basePtrType + " " +
		  basePtr->getIRName();

	for (auto * index: getIndices()) {
		str += ", " + index->getType()->toString() + " " + index->getIRName();
	}
}
