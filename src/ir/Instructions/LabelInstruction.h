#pragma once

#include <string>

#include "Instruction.h"

class Function;

///
/// @brief Label指令
///
class LabelInstruction : public Instruction {

public:
	///
	/// @brief 构造函数
	/// @param _func 所属函数
	///
	explicit LabelInstruction(Function * _func);

	///
	/// @brief 转换成字符串
	/// @param str 返回指令字符串
	///
	void toString(std::string & str) override;
};
