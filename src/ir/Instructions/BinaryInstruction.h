// 二元操作指令，如加和减

#pragma once

#include "Instruction.h"

///
/// @brief 二元运算指令
///
class BinaryInstruction : public Instruction {

public:
	/// @brief 构造函数
	/// @param _op 操作符
	/// @param _result 结果操作数
	/// @param _srcVal1 源操作数1
	/// @param _srcVal2 源操作数2
	BinaryInstruction(Function * _func, IRInstOperator _op, Value * _srcVal1, Value * _srcVal2, Type * _type);

	/// @brief 转换成字符串
	void toString(std::string & str) override;
};
