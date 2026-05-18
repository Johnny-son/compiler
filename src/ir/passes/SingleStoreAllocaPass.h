// Forward loads from stack slots with one dominating store.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class SingleStoreAllocaPass final : public IRPass {
public:
	std::string name() const override
	{
		return "single-store-alloca";
	}

	bool run(Module & module) override;
};
