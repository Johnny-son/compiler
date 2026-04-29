// 函数出口或返回指令

#include "ir/Types/VoidType.h"

#include "ExitInstruction.h"

/// @brief return语句指令
/// @param _result 返回结果值
ExitInstruction::ExitInstruction(Function * _func, Value * _result)
	: Instruction(_func, IRInstOperator::IRINST_OP_EXIT, VoidType::getType())
{
	if (_result != nullptr) {
		addOperand(_result);
	}
}

/// @brief 转换成字符串显示
/// @param str 转换后的字符串
void ExitInstruction::toString(std::string & str)
{
	if (getOperandsNum() == 0) {
		str = "exit";
	} else {
		Value * src1 = getOperand(0);
		str = "exit " + src1->getIRName();
	}
}
