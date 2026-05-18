// Conservative dead store elimination.

#pragma once

#include "IRPass.h"

class DSEPass : public IRPass {
public:
	bool run(Module & module) override;
	std::string name() const override { return "DSE"; }
};
