// Conservative loop-invariant code motion.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class LICMPass final : public IRPass {
public:
	std::string name() const override
	{
		return "licm";
	}

	bool run(Module & module) override;
};
