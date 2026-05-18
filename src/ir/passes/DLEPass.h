// Redundant load elimination using conservative memory alias information.

#pragma once

#include "IRPass.h"

class DLEPass : public IRPass {
public:
	bool run(Module & module) override;
	std::string name() const override { return "DLE"; }
};
