#pragma once

#include <string>

#include "Function.h"
#include "Instruction.h"
#include "LabelInstruction.h"

/// @brief 条件跳转指令，cond非0时跳转trueTarget，否则跳转falseTarget
class CondBranchInstruction final : public Instruction {
public:
	/// @brief 构造条件跳转指令
	/// @param _func 所属函数
	/// @param _cond 条件值
	/// @param _trueTarget 条件为真时的目标标签
	/// @param _falseTarget 条件为假时的目标标签
	CondBranchInstruction(Function * _func, Value * _cond, LabelInstruction * _trueTarget, LabelInstruction * _falseTarget);

	/// @brief 输出DragonIR形式的条件跳转文本
	/// @param str 输出字符串
	void toString(std::string & str) override;

	/// @brief 获取条件值
	[[nodiscard]] Value * getCondition();

	/// @brief 获取true分支目标
	[[nodiscard]] LabelInstruction * getTrueTarget() const;

	/// @brief 获取false分支目标
	[[nodiscard]] LabelInstruction * getFalseTarget() const;

private:
	LabelInstruction * trueTarget;
	LabelInstruction * falseTarget;
};
