// LLVM br指令

#include "BranchInst.h"

#include "BasicBlock.h"
#include "ir/Types/VoidType.h"

BranchInst::BranchInst(Function * func, BasicBlock * target)
	: Instruction(func, IRInstOperator::IRINST_OP_MAX, VoidType::getType()), trueTarget(target)
{}

BranchInst::BranchInst(Function * func, Value * cond, BasicBlock * trueTarget, BasicBlock * falseTarget)
	: Instruction(func, IRInstOperator::IRINST_OP_MAX, VoidType::getType()), trueTarget(trueTarget), falseTarget(falseTarget)
{
	addOperand(cond);
}

bool BranchInst::isConditional() const
{
	return falseTarget != nullptr;
}

bool BranchInst::isTerminator() const
{
	return true;
}

void BranchInst::toString(std::string & str)
{
	if (!isConditional()) {
		str = "br label %" + trueTarget->getIRName();
		return;
	}

	Value * cond = getOperand(0);
	str = "br " + cond->getType()->toString() + " " + cond->getIRName() + ", label %" + trueTarget->getIRName() +
		  ", label %" + falseTarget->getIRName();
}
