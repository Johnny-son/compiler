// Convert simple self tail calls to loops.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class TailCallOptPass final : public IRPass {
public:
	std::string name() const override
	{
		return "tail-call-opt";
	}

	bool run(Module & module) override;
};
