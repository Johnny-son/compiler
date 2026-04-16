#pragma once

#include <string>

#include "ir/Values/RegVariable.h"

#define RISCV64_A0_REG_NO 0
#define RISCV64_A1_REG_NO 1
#define RISCV64_A2_REG_NO 2
#define RISCV64_A3_REG_NO 3
#define RISCV64_A4_REG_NO 4
#define RISCV64_A5_REG_NO 5
#define RISCV64_A6_REG_NO 6
#define RISCV64_A7_REG_NO 7
#define RISCV64_T0_REG_NO 8
#define RISCV64_T1_REG_NO 9
#define RISCV64_TMP_REG_NO 10
#define RISCV64_FP_REG_NO 11
#define RISCV64_T3_REG_NO 12
#define RISCV64_SP_REG_NO 13
#define RISCV64_RA_REG_NO 14
#define RISCV64_T4_REG_NO 15

class PlatformRiscv64 {

public:
	static constexpr int maxRegNum = 16;
	static constexpr int maxUsableRegNum = 11;
	static constexpr int argRegCount = 8;
	static constexpr int stackSlotSize = 8;
	static constexpr int stackAlign = 16;
	static constexpr int savedAreaSize = 16;

	static bool constExpr(int num);

	static bool isDisp(int num);

	static bool isReg(const std::string & name);

	static const std::string regName[maxRegNum];

	static RegVariable * intRegVal[PlatformRiscv64::maxRegNum];
};
