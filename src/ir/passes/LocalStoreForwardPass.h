// Local store-to-load forwarding.

#pragma once

#include <string>

#include "IRPass.h"

class Module;

class LocalStoreForwardPass final : public IRPass {
public:
	std::string name() const override
	{
		return "local-store-forward";
	}

	bool run(Module & module) override;
};
