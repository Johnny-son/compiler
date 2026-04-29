// 数组类型描述类

#pragma once

#include <cstdint>
#include <string>

#include "Type.h"

class ArrayType final : public Type {
public:
	ArrayType(Type * elementType, int32_t elementCount)
		: Type(Type::ArrayTyID), elementType(elementType), elementCount(elementCount)
	{}

	Type * getElementType() const
	{
		return elementType;
	}

	int32_t getElementCount() const
	{
		return elementCount;
	}

	[[nodiscard]] int32_t getSize() const override
	{
		return elementType->getSize() * elementCount;
	}

	[[nodiscard]] std::string toString() const override
	{
		return "[" + std::to_string(elementCount) + " x " + elementType->toString() + "]";
	}

private:
	Type * elementType = nullptr;
	int32_t elementCount = 0;
};
