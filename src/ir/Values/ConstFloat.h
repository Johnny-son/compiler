// float类型的常量

#pragma once

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include "Constant.h"
#include "ir/Types/FloatType.h"

class ConstFloat : public Constant {
public:
	explicit ConstFloat(float val) : Constant(FloatType::getTypeFloat()), floatVal(val)
	{
		double asDouble = static_cast<double>(val);
		uint64_t bits = 0;
		std::memcpy(&bits, &asDouble, sizeof(bits));
		std::ostringstream oss;
		oss << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << bits;
		name = oss.str();
	}

	[[nodiscard]] std::string getIRName() const override
	{
		return name;
	}

	float getVal() const
	{
		return floatVal;
	}

private:
	float floatVal;
};
