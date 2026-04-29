// Label名称符号类

#pragma once

#include "Type.h"

///
/// @brief LabelType 是一种特殊的 Type，它表示基本块（BasicBlock）标签的类型。也可用于Label指令的类型。
///
class LabelType final : public Type {

public:
	///
	/// @brief 获取类型，全局只有一份
	/// @return VoidType*
	///
	static LabelType * getType();

	///
	/// @brief 获取类型的IR标识符
	/// @return std::string IR标识符void
	///
	[[nodiscard]] std::string toString() const override
	{
		return "void";
	}

private:
	///
	/// @brief 构造函数
	///
	explicit LabelType() : Type(Type::LabelTyID)
	{}
};
