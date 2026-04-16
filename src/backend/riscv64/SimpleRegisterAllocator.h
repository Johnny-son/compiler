#pragma once

#include <bitset>
#include <vector>

#include "PlatformRiscv64.h"
#include "Value.h"

class SimpleRegisterAllocator {

public:
	SimpleRegisterAllocator();

	int Allocate(Value * var = nullptr, int32_t no = -1);

	void Allocate(int32_t no);

	void free(Value * var);

	void free(int32_t no);

protected:
	void bitmapSet(int32_t no);

protected:
	std::bitset<PlatformRiscv64::maxUsableRegNum> regBitmap;

	std::vector<Value *> regValues;

	std::bitset<PlatformRiscv64::maxUsableRegNum> usedBitmap;
};
