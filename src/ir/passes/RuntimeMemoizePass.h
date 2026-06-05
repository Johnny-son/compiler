// Runtime memoization for simple pure self-recursive functions.

#pragma once

#include <string>

#include "IRPass.h"

class RuntimeMemoizePass final : public IRPass {
public:
	std::string name() const override
	{
		return "runtime-memoize";
	}

	bool run(Module & module) override;
};
