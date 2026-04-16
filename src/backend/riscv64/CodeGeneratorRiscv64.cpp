#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "CodeGeneratorRiscv64.h"
#include "Function.h"
#include "ILocRiscv64.h"
#include "InstSelectorRiscv64.h"
#include "Module.h"
#include "PlatformRiscv64.h"
#include "ir/Values/FormalParam.h"
#include "ir/Values/GlobalVariable.h"

namespace {

int alignTo(int value, int alignment)
{
	return (value + alignment - 1) & -alignment;
}

int slotSizeForType(Type * type)
{
	if ((type != nullptr) && type->isPointerType()) {
		return 8;
	}

	if (type != nullptr) {
		const int size = type->getSize();
		if (size > 0) {
			return std::max(4, size);
		}
	}

	return 4;
}

} // namespace

CodeGeneratorRiscv64::CodeGeneratorRiscv64(Module * _module) : CodeGeneratorAsm(_module)
{}

CodeGeneratorRiscv64::~CodeGeneratorRiscv64()
{}

void CodeGeneratorRiscv64::genHeader()
{
	fprintf(fp, ".text\n");
}

void CodeGeneratorRiscv64::genDataSection()
{
	bool enteredData = false;
	bool enteredBss = false;

	for (auto * var: module->getGlobalVariables()) {
		if (var->isInBSSSection()) {
			if (!enteredBss) {
				fprintf(fp, ".bss\n");
				enteredBss = true;
			}

			fprintf(fp, ".globl %s\n", var->getName().c_str());
			fprintf(fp, ".balign %d\n", var->getAlignment());
			fprintf(fp, "%s:\n", var->getName().c_str());
			fprintf(fp, ".zero %d\n", std::max(4, var->getType()->getSize()));
			continue;
		}

		if (!enteredData) {
			fprintf(fp, ".data\n");
			enteredData = true;
		}

		fprintf(fp, ".globl %s\n", var->getName().c_str());
		fprintf(fp, ".balign %d\n", var->getAlignment());
		fprintf(fp, "%s:\n", var->getName().c_str());
		fprintf(fp, ".word %d\n", var->hasInitializerValue() ? var->getInitializerInt() : 0);
	}
}

void CodeGeneratorRiscv64::getIRValueStr(Value * val, std::string & str)
{
	std::string name = val->getName();
	std::string IRName = val->getIRName();
	int32_t regId = val->getRegId();
	int32_t baseRegId = -1;
	int64_t offset = 0;
	std::string showName;

	if (name.empty() && !IRName.empty()) {
		showName = IRName;
	} else if (!name.empty() && IRName.empty()) {
		showName = name;
	} else if (!name.empty() && !IRName.empty()) {
		showName = name + ":" + IRName;
	}

	if (regId != -1) {
		str += "\t# " + showName + ":" + PlatformRiscv64::regName[regId];
	} else if (val->getMemoryAddr(&baseRegId, &offset)) {
		str += "\t# " + showName + ":" + std::to_string(offset) + "(" + PlatformRiscv64::regName[baseRegId] + ")";
	}
}

void CodeGeneratorRiscv64::genCodeSection(Function * func)
{
	registerAllocation(func);

	auto & irInsts = func->getInterCode().getInsts();
	for (auto * inst: irInsts) {
		if (inst->getOp() == IRInstOperator::IRINST_OP_LABEL) {
			inst->setName(IR_LABEL_PREFIX + std::to_string(labelIndex++));
		}
	}

	ILocRiscv64 iloc(module);
	InstSelectorRiscv64 instSelector(irInsts, iloc, func, simpleRegisterAllocator);
	instSelector.setShowLinearIR(this->showLinearIR);
	instSelector.run();
	iloc.deleteUnusedLabel();

	fprintf(fp, ".text\n");
	fprintf(fp, ".balign %d\n", std::max(1, func->getAlignment()));
	fprintf(fp, ".globl %s\n", func->getName().c_str());
	fprintf(fp, ".type %s, @function\n", func->getName().c_str());
	fprintf(fp, "%s:\n", func->getName().c_str());

	if (this->showLinearIR) {
		for (auto * param: func->getParams()) {
			std::string str;
			getIRValueStr(param, str);
			if (!str.empty()) {
				fprintf(fp, "%s\n", str.c_str());
			}
		}

		for (auto * localVar: func->getVarValues()) {
			std::string str;
			getIRValueStr(localVar, str);
			if (!str.empty()) {
				fprintf(fp, "%s\n", str.c_str());
			}
		}

		for (auto * inst: irInsts) {
			if (inst->hasResultValue()) {
				std::string str;
				getIRValueStr(inst, str);
				if (!str.empty()) {
					fprintf(fp, "%s\n", str.c_str());
				}
			}
		}
	}

	iloc.outPut(fp);
	fprintf(fp, ".size %s, .-%s\n", func->getName().c_str(), func->getName().c_str());
}

void CodeGeneratorRiscv64::registerAllocation(Function * func)
{
	if (func->isBuiltin()) {
		return;
	}

	auto & protectedRegNo = func->getProtectedReg();
	protectedRegNo.clear();
	protectedRegNo.push_back(RISCV64_FP_REG_NO);
	protectedRegNo.push_back(RISCV64_RA_REG_NO);

	stackAlloc(func);
	adjustFormalParamInsts(func);
	adjustFuncCallInsts(func);
}

void CodeGeneratorRiscv64::adjustFormalParamInsts(Function * func)
{
	auto & params = func->getParams();

	for (int k = 0; k < static_cast<int>(params.size()); ++k) {
		params[k]->setRegId(-1);

		if (k >= PlatformRiscv64::argRegCount) {
			params[k]->setMemoryAddr(
				RISCV64_FP_REG_NO,
				(k - PlatformRiscv64::argRegCount) * PlatformRiscv64::stackSlotSize);
		}
	}
}

void CodeGeneratorRiscv64::adjustFuncCallInsts(Function *)
{}

void CodeGeneratorRiscv64::stackAlloc(Function * func)
{
	int localCursor = PlatformRiscv64::savedAreaSize;

	auto & params = func->getParams();
	for (int k = 0; k < static_cast<int>(params.size()) && k < PlatformRiscv64::argRegCount; ++k) {
		const int size = slotSizeForType(params[k]->getType());
		localCursor = alignTo(localCursor, std::min(size, 8));
		localCursor += size;
		params[k]->setMemoryAddr(RISCV64_FP_REG_NO, -localCursor);
	}

	for (auto * var: func->getVarValues()) {
		if ((var->getRegId() == -1) && !var->getMemoryAddr()) {
			const int size = slotSizeForType(var->getType());
			localCursor = alignTo(localCursor, std::min(size, 8));
			localCursor += size;
			var->setMemoryAddr(RISCV64_FP_REG_NO, -localCursor);
		}
	}

	for (auto * inst: func->getInterCode().getInsts()) {
		if (inst->hasResultValue() && (inst->getRegId() == -1) && !inst->getMemoryAddr()) {
			const int size = slotSizeForType(inst->getType());
			localCursor = alignTo(localCursor, std::min(size, 8));
			localCursor += size;
			inst->setMemoryAddr(RISCV64_FP_REG_NO, -localCursor);
		}
	}

	int outgoingArea = 0;
	const int maxFuncCallArgCnt = func->getMaxFuncCallArgCnt();
	if (maxFuncCallArgCnt > PlatformRiscv64::argRegCount) {
		outgoingArea = (maxFuncCallArgCnt - PlatformRiscv64::argRegCount) * PlatformRiscv64::stackSlotSize;
	}

	const int totalFrameSize = alignTo(localCursor + outgoingArea, PlatformRiscv64::stackAlign);
	func->setMaxDep(std::max(totalFrameSize, PlatformRiscv64::savedAreaSize));
}
