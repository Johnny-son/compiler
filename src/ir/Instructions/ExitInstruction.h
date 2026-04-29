// 函数出口或返回指令

#pragma once

#include "Instruction.h"

///
/// @brief 函数的出口指令，用来指代函数返回指令
///
class ExitInstruction : public Instruction {

public:
	///
	/// @brief 构造函数
	/// @param _func 所属的函数
	/// @param result 函数的返回值
	ExitInstruction(Function * _func, Value * result = nullptr);

	/// @brief 转换成字符串
	void toString(std::string & str) override;
};
