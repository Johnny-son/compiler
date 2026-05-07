// Simplify control-flow graph structure.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class SimplifyCFGPass final : public IRPass {
public:
	std::string name() const override
	{
		return "simplify-cfg";
	}

	bool run(Module & module) override;
};
