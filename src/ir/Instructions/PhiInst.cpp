// LLVM phi指令

#include "PhiInst.h"

#include "BasicBlock.h"

PhiInst::PhiInst(Function * func, Type * type, const std::string & name)
	: Instruction(func, IRInstOperator::IRINST_OP_MAX, type)
{
	this->IRName = name;
}

void PhiInst::addIncoming(Value * value, BasicBlock * block)
{
	incomingValues.emplace_back(value, block);
	addOperand(value);
}

const std::vector<std::pair<Value *, BasicBlock *>> & PhiInst::getIncomingValues() const
{
	return incomingValues;
}

void PhiInst::toString(std::string & str)
{
	str = getIRName() + " = phi " + getType()->toString() + " ";
	for (size_t idx = 0; idx < incomingValues.size(); ++idx) {
		if (idx != 0) {
			str += ", ";
		}
		str += "[ " + incomingValues[idx].first->getIRName() + ", %" + incomingValues[idx].second->getIRName() + " ]";
	}
}
