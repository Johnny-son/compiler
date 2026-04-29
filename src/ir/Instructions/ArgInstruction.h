// 函数实参ARG指令

#pragma once

#include "Instruction.h"

///
/// @brief 函数调用实参指令ARG
///
class ArgInstruction : public Instruction {

public:
	/// @brief 函数实参指令
	/// @param src 实参结果变量
	ArgInstruction(Function * _func, Value * src);

	/// @brief 转换成字符串
	void toString(std::string & str) override;
};
