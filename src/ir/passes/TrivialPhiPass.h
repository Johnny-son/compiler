// Eliminate phi nodes that always choose the same value.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class TrivialPhiPass final : public IRPass {
public:
	std::string name() const override
	{
		return "trivial-phi";
	}

	bool run(Module & module) override;
};
