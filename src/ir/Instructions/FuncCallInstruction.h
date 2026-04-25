#pragma once

#include <vector>

#include "Type.h"
#include "Instruction.h"

class Function;

///
/// @brief 函数调用指令
///
class FuncCallInstruction : public Instruction {

public:
	///
	/// @brief 函数调用时的被调用函数
	///
	Function * calledFunction = nullptr;

public:
	/// @brief 含有参数的函数调用
	/// @param srcVal 函数的实参Value
	/// @param result 保存返回值的Value
	FuncCallInstruction(Function * _func, Function * calledFunc, std::vector<Value *> & _srcVal, Type * _type);

	///
	/// @brief 转换成IR指令文本
	/// @param str IR指令
	///
	void toString(std::string & str) override;

	///
	/// @brief 获取被调用函数的名字
	/// @return std::string 被调用函数名字
	///
	[[nodiscard]] std::string getCalledName() const;
};
