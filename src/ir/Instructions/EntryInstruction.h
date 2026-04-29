// 函数的入口指令

#pragma once

#include "Instruction.h"

///
/// @brief 函数入口指令
///
class EntryInstruction : public Instruction {

public:
	///
	/// @brief 构造函数
	///
	EntryInstruction(Function * _func);

	///
	/// @brief 转换成IR指令字符串
	///
	void toString(std::string & str) override;
};
