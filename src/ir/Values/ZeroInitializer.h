// 聚合零初始化常量

#pragma once

#include "Constant.h"

class ZeroInitializer : public Constant {
public:
	explicit ZeroInitializer(Type * type) : Constant(type)
	{}

	[[nodiscard]] std::string getIRName() const override
	{
		return "zeroinitializer";
	}
};
