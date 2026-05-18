// Conservative loop canonicalization.

#pragma once

#include "IRPass.h"

class LoopCanonicalizePass : public IRPass {
public:
	std::string name() const override { return "canonicalize-loop"; }
	bool run(Module & module) override;
};
