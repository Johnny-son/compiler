// float类型类

#pragma once

#include "Type.h"

class FloatType final : public Type {
public:
	static FloatType * getTypeFloat();

	[[nodiscard]] std::string toString() const override
	{
		return "float";
	}

	[[nodiscard]] int32_t getSize() const override
	{
		return 4;
	}

private:
	FloatType() : Type(Type::FloatTyID)
	{}
};
