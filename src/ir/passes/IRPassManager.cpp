// Small pass manager for module-level IR optimizations.

#include "IRPassManager.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include "BasicBlock.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"

namespace {

bool shouldPrintStats()
{
	const char * value = std::getenv("MINIC_IR_PASS_STATS");
	return value != nullptr && value[0] != '\0' && std::string(value) != "0";
}

void printStatsRecord(const IRPassRunRecord & record)
{
	std::cerr << "[ir-pass-stats] pass=" << record.passName << " success=" << (record.success ? "yes" : "no")
			  << " changed=" << (record.sizeChanged() ? "yes" : "no") << " functions=" << record.before.functions
			  << "->" << record.after.functions << " blocks=" << record.before.basicBlocks << "->"
			  << record.after.basicBlocks << " instructions=" << record.before.instructions << "->"
			  << record.after.instructions << " result_values=" << record.before.resultValues << "->"
			  << record.after.resultValues << " terminators=" << record.before.terminators << "->"
			  << record.after.terminators << '\n';
}

} // namespace

IRModuleStats IRPassManager::collectStats(Module & module)
{
	IRModuleStats stats;
	for (auto * function: module.getFunctionList()) {
		if (function == nullptr || function->isBuiltin()) {
			continue;
		}

		++stats.functions;
		for (auto * block: function->getBasicBlocks()) {
			if (block == nullptr) {
				continue;
			}

			++stats.basicBlocks;
			for (auto * inst: block->getInstructions()) {
				if (inst == nullptr) {
					continue;
				}

				++stats.instructions;
				if (inst->isTerminator()) {
					++stats.terminators;
				}
				if (inst->hasResultValue()) {
					++stats.resultValues;
				}
			}
		}
	}
	return stats;
}

bool IRPassManager::run(Module & module)
{
	lastRunRecords.clear();
	const bool printStats = shouldPrintStats();

	for (auto & pass: passes) {
		if (pass == nullptr) {
			continue;
		}

		IRPassRunRecord record;
		record.passName = pass->name();
		record.before = collectStats(module);
		record.success = pass->run(module);
		record.after = collectStats(module);
		lastRunRecords.push_back(record);

		if (printStats) {
			printStatsRecord(record);
		}

		if (!record.success) {
			return false;
		}
	}
	return true;
}

bool IRPassManager::runUntilStable(Module & module, std::size_t maxRounds)
{
	for (std::size_t round = 0; round < maxRounds; ++round) {
		const IRModuleStats before = collectStats(module);
		if (!run(module)) {
			return false;
		}
		const IRModuleStats after = collectStats(module);
		if (after == before) {
			return true;
		}
	}
	return true;
}
