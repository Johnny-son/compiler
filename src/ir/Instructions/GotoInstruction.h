// 无条件跳转指令即goto指令

#pragma once

#include <string>

#include "Instruction.h"
#include "LabelInstruction.h"
#include "Function.h"

///
/// @brief 无条件跳转指令
///
class GotoInstruction final : public Instruction {

public:
	///
	/// @brief 无条件跳转指令的构造函数
	/// @param target 跳转目标
	///
	GotoInstruction(Function * _func, Instruction * _target);

	/// @brief 转换成字符串
	void toString(std::string & str) override;

	///
	/// @brief 获取目标Label指令
	/// @return LabelInstruction*
	///
	[[nodiscard]] LabelInstruction * getTarget() const;

private:
	///
	/// @brief 跳转到的目标Label指令
	///
	LabelInstruction * target;
};
