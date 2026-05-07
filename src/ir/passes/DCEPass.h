// Remove trivially dead IR instructions.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class DCEPass final : public IRPass {
public:
	std::string name() const override
	{
		return "dce";
	}

	bool run(Module & module) override;
};
