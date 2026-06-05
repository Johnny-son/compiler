// Conservative aggressive dead code elimination.

#pragma once

#include "IRPass.h"

class ADCEPass final : public IRPass {
public:
	std::string name() const override { return "ADCE"; }
	bool run(Module & module) override;
};
