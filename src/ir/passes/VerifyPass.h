// Check basic structural invariants of the LLVM-style IR.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class VerifyPass final : public IRPass {
public:
	std::string name() const override
	{
		return "verify";
	}

	bool run(Module & module) override;
};
