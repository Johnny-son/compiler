#pragma once

#include <cstdint>

#include "Type.h"

class IntegerType final : public Type {

public:
	///
	/// @brief 获取类型，全局只有一份
	/// @return VoidType*
	///
	static IntegerType * getTypeBool();

	///
	/// @brief 获取类型，全局只有一份
	/// @return VoidType*
	///
	static IntegerType * getTypeInt();

	///
	/// @brief 获取类型的IR标识符
	/// @return std::string IR标识符void
	///
	[[nodiscard]] std::string toString() const override
	{
		return "i" + std::to_string(bitWidth);
	}

	///
	/// @brief 获取整数的位宽
	/// @return int32_t
	///
	[[nodiscard]] int32_t getBitWidth() const
	{
		return this->bitWidth;
	}

	///
	/// @brief 是否是布尔类型，也就是1位整数类型
	/// @return true
	/// @return false
	///
	[[nodiscard]] bool isInt1Byte() const override
	{
		return bitWidth == 1;
	}

	///
	/// @brief 是否是int类型，也就是32位整数类型
	/// @return true
	/// @return false
	///
	[[nodiscard]] bool isInt32Type() const override
	{
		return bitWidth == 32;
	}

	///
	/// @brief 获得类型所占内存空间大小
	/// @return int32_t
	///
	[[nodiscard]] int32_t getSize() const override
	{
		return 4;
	}

private:
	///
	/// @brief 构造函数
	///
	explicit IntegerType(int32_t _bitWidth) : Type(Type::IntegerTyID), bitWidth(_bitWidth)
	{}

	///
	/// @brief 位宽
	///
	int32_t bitWidth;
};
