#include "ir/Types/VoidType.h"

#include "GotoInstruction.h"

///
/// @brief 无条件跳转指令的构造函数
/// @param target 跳转目标
///
GotoInstruction::GotoInstruction(Function * _func, Instruction * _target)
	: Instruction(_func, IRInstOperator::IRINST_OP_GOTO, VoidType::getType())
{
	// 真假目标一样，则无条件跳转
	target = static_cast<LabelInstruction *>(_target);
}

/// @brief 转换成IR指令文本
void GotoInstruction::toString(std::string & str)
{
	str = "br label " + target->getIRName();
}

///
/// @brief 获取目标Label指令
/// @return LabelInstruction* label指令
///
LabelInstruction * GotoInstruction::getTarget() const
{
	return target;
}
