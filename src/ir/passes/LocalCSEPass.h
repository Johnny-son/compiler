// Local common subexpression elimination for pure IR instructions.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class LocalCSEPass final : public IRPass {
public:
	std::string name() const override
	{
		return "local-cse";
	}

	bool run(Module & module) override;
};
