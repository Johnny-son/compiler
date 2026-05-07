// Fold simple constant expressions in IR.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class ConstantFoldPass final : public IRPass {
public:
	std::string name() const override
	{
		return "constant-fold";
	}

	bool run(Module & module) override;
};
