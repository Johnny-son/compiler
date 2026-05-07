// Promote eligible stack slots to SSA values.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class Mem2RegPass final : public IRPass {
public:
	std::string name() const override
	{
		return "mem2reg";
	}

	bool run(Module & module) override;

	[[nodiscard]] int promotedAllocaCount() const
	{
		return promotedAllocas;
	}

	[[nodiscard]] int insertedPhiCount() const
	{
		return insertedPhis;
	}

private:
	int promotedAllocas = 0;
	int skippedAllocas = 0;
	int insertedPhis = 0;
	int removedLoads = 0;
	int removedStores = 0;
};
