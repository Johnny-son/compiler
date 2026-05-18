// Inline tiny pure single-block functions.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class InlineSimpleFunctionPass final : public IRPass {
public:
	std::string name() const override
	{
		return "inline-simple-function";
	}

	bool run(Module & module) override;
};
