#include "backend/include/BackendDriver.h"

#include <cstdio>

#include "backend/include/Asm.h"
#include "backend/include/FrameLayout.h"
#include "backend/include/IRAdapter.h"
#include "backend/include/InstructionSelector.h"
#include "backend/include/MachineAsmLowering.h"
#include "backend/include/MachineCFG.h"
#include "backend/include/NaiveRegisterAllocator.h"
#include "ir/include/Module.h"
#include "ir/Values/GlobalVariable.h"

bool BackendDriver::run(Module * module, const std::string & outputFile) const
{
	if (module == nullptr) {
		return false;
	}

	FILE * fp = fopen(outputFile.c_str(), "w");
	if (fp == nullptr) {
		return false;
	}

	IRModuleView moduleView = IRAdapter::adapt(module);

	bool emittedData = false;
	bool emittedBss = false;
	for (const auto & global: moduleView.globals()) {
		if (!global.valid()) {
			continue;
		}

		auto * globalVar = dynamic_cast<GlobalVariable *>(global.raw());
		if (globalVar == nullptr) {
			continue;
		}

		if (globalVar->isInBSSSection()) {
			if (!emittedBss) {
				fprintf(fp, ".bss\n");
				emittedBss = true;
			}

			fprintf(fp, ".globl %s\n", global.name().c_str());
			fprintf(fp, ".balign %d\n", globalVar->getAlignment());
			fprintf(fp, "%s:\n", global.name().c_str());
			fprintf(fp, ".zero %d\n", globalVar->getType()->getSize());
			continue;
		}

		if (!emittedData) {
			fprintf(fp, ".data\n");
			emittedData = true;
		}

		fprintf(fp, ".globl %s\n", global.name().c_str());
		fprintf(fp, ".balign %d\n", globalVar->getAlignment());
		fprintf(fp, "%s:\n", global.name().c_str());
		fprintf(fp, ".word %d\n", globalVar->hasInitializerValue() ? globalVar->getInitializerInt() : 0);
	}

	for (const auto & function: moduleView.functions()) {
		if (!function.valid() || function.isBuiltin()) {
			continue;
		}

		FunctionFrameLayout layout = FrameLayoutBuilder::build(function);
		InstructionSelector selector(function, layout);
		MachineFunction machineFunction = selector.run();
		MachineCFGBuilder cfgBuilder;
		cfgBuilder.run(machineFunction);
		NaiveRegisterAllocator allocator;
		MachineFunction allocatedFunction = allocator.run(machineFunction);
		MachineAsmLowering lowering(allocatedFunction, layout);
		AsmFunction asmFunction = lowering.run();
		AsmPrinter::printFunction(fp, asmFunction);
	}

	fclose(fp);
	return true;
}
