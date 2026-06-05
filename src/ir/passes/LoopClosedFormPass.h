// Closed-form replacement for very small scalar recurrence loops.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class LoopClosedFormPass final : public IRPass {
public:
	std::string name() const override
	{
		return "loop-closed-form";
	}

	bool run(Module & module) override;
};
