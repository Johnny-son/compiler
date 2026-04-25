#include "ir/Types/VoidType.h"

#include "LabelInstruction.h"

///
/// @brief 构造函数
/// @param _func 所属函数
///
LabelInstruction::LabelInstruction(Function * _func)
	: Instruction(_func, IRInstOperator::IRINST_OP_LABEL, VoidType::getType())
{}

/// @brief 转换成字符串
/// @param str 返回指令字符串
void LabelInstruction::toString(std::string & str)
{
	str = IRName + ":";
}
