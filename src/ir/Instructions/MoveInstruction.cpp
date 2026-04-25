#include "ir/Types/VoidType.h"

#include "MoveInstruction.h"

///
/// @brief 构造函数
/// @param _func 所属的函数
/// @param result 结构操作数
/// @param srcVal1 源操作数
///
MoveInstruction::MoveInstruction(Function * _func, Value * _result, Value * _srcVal1)
	: Instruction(_func, IRInstOperator::IRINST_OP_ASSIGN, VoidType::getType())
{
	addOperand(_result);
	addOperand(_srcVal1);
}

/// @brief 转换成字符串显示
/// @param str 转换后的字符串
void MoveInstruction::toString(std::string & str)
{

	Value *dstVal = getOperand(0), *srcVal = getOperand(1);

	str = dstVal->getIRName() + " = " + srcVal->getIRName();
}
