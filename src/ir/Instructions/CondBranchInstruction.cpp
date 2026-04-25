#include "CondBranchInstruction.h"
#include "ir/Types/VoidType.h"

/// @brief 构造条件跳转指令
/// @param _func 所属函数
/// @param _cond 条件值
/// @param _trueTarget true分支目标
/// @param _falseTarget false分支目标
CondBranchInstruction::CondBranchInstruction(
	Function * _func, Value * _cond, LabelInstruction * _trueTarget, LabelInstruction * _falseTarget)
	: Instruction(_func, IRInstOperator::IRINST_OP_COND_BR, VoidType::getType()),
	  trueTarget(_trueTarget),
	  falseTarget(_falseTarget)
{
	addOperand(_cond);
}

/// @brief 转换成字符串
/// @param str 转换后的字符串
void CondBranchInstruction::toString(std::string & str)
{
	str = "br " + getOperand(0)->getIRName() + ", label " + trueTarget->getIRName() + ", label " + falseTarget->getIRName();
}

/// @brief 获取条件值
/// @return 条件Value
Value * CondBranchInstruction::getCondition()
{
	return getOperand(0);
}

/// @brief 获取true分支目标
/// @return 标签指令
LabelInstruction * CondBranchInstruction::getTrueTarget() const
{
	return trueTarget;
}

/// @brief 获取false分支目标
/// @return 标签指令
LabelInstruction * CondBranchInstruction::getFalseTarget() const
{
	return falseTarget;
}
