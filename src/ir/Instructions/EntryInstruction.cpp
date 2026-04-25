#include "EntryInstruction.h"
#include "ir/Types/VoidType.h"

/// @brief return语句指令
EntryInstruction::EntryInstruction(Function * _func)
	: Instruction(_func, IRInstOperator::IRINST_OP_ENTRY, VoidType::getType())
{}

/// @brief 转换成字符串
void EntryInstruction::toString(std::string & str)
{
	str = "entry";
}
