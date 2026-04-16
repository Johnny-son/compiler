#include "PlatformRiscv64.h"

#include "ir/Types/IntegerType.h"

const std::string PlatformRiscv64::regName[PlatformRiscv64::maxRegNum] = {
	"a0",
	"a1",
	"a2",
	"a3",
	"a4",
	"a5",
	"a6",
	"a7",
	"t0",
	"t1",
	"t2",
	"s0",
	"t3",
	"sp",
	"ra",
	"t4",
};

RegVariable * PlatformRiscv64::intRegVal[PlatformRiscv64::maxRegNum] = {
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[0], 0),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[1], 1),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[2], 2),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[3], 3),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[4], 4),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[5], 5),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[6], 6),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[7], 7),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[8], 8),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[9], 9),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[10], 10),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[11], 11),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[12], 12),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[13], 13),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[14], 14),
	new RegVariable(IntegerType::getTypeInt(), PlatformRiscv64::regName[15], 15),
};

bool PlatformRiscv64::constExpr(int num)
{
	return num >= -2048 && num <= 2047;
}

bool PlatformRiscv64::isDisp(int num)
{
	return constExpr(num);
}

bool PlatformRiscv64::isReg(const std::string & name)
{
	for (const auto & reg: regName) {
		if (reg == name) {
			return true;
		}
	}

	return false;
}
