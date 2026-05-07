// LLVM phi指令

#include "PhiInst.h"

#include "BasicBlock.h"

PhiInst::PhiInst(Function * func, Type * type, const std::string & name)
	: Instruction(func, type)
{
	this->IRName = name;
}

void PhiInst::addIncoming(Value * value, BasicBlock * block)
{
	incomingValues.emplace_back(value, block);
	addOperand(value);
}

bool PhiInst::removeIncomingFrom(BasicBlock * block)
{
	bool changed = false;
	for (int index = static_cast<int>(incomingValues.size()) - 1; index >= 0; --index) {
		if (incomingValues[static_cast<std::size_t>(index)].second != block) {
			continue;
		}

		removeOperand(index);
		incomingValues.erase(incomingValues.begin() + index);
		changed = true;
	}
	return changed;
}

void PhiInst::replaceIncomingBlock(BasicBlock * oldBlock, BasicBlock * newBlock)
{
	for (auto & incoming: incomingValues) {
		if (incoming.second == oldBlock) {
			incoming.second = newBlock;
		}
	}
}

void PhiInst::replaceIncomingValue(Value * oldValue, Value * newValue)
{
	for (auto & incoming: incomingValues) {
		if (incoming.first == oldValue) {
			incoming.first = newValue;
		}
	}
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
