// Small pass manager for module-level IR optimizations.

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "IRPass.h"

class Module;

struct IRModuleStats {
	std::size_t functions = 0;
	std::size_t basicBlocks = 0;
	std::size_t instructions = 0;
	std::size_t terminators = 0;
	std::size_t resultValues = 0;

	bool operator==(const IRModuleStats & other) const
	{
		return functions == other.functions && basicBlocks == other.basicBlocks && instructions == other.instructions &&
			   terminators == other.terminators && resultValues == other.resultValues;
	}

	bool operator!=(const IRModuleStats & other) const
	{
		return !(*this == other);
	}
};

struct IRPassRunRecord {
	std::string passName;
	IRModuleStats before;
	IRModuleStats after;
	bool success = true;

	bool sizeChanged() const
	{
		return before != after;
	}
};

class IRPassManager {
public:
	template<typename PassT, typename... Args>
	PassT * addPass(Args &&... args)
	{
		auto pass = std::make_unique<PassT>(std::forward<Args>(args)...);
		auto * raw = pass.get();
		passes.push_back(std::move(pass));
		return raw;
	}

	bool run(Module & module);
	bool runUntilStable(Module & module, std::size_t maxRounds);

	const std::vector<IRPassRunRecord> & getLastRunRecords() const
	{
		return lastRunRecords;
	}

	static IRModuleStats collectStats(Module & module);

private:
	std::vector<std::unique_ptr<IRPass>> passes;
	std::vector<IRPassRunRecord> lastRunRecords;
};
