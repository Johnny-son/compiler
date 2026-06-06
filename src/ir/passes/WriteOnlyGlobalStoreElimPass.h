#pragma once

#include "IRPass.h"

class Module;

class WriteOnlyGlobalStoreElimPass final : public IRPass {
public:
	std::string name() const override
	{
		return "WriteOnlyGlobalStoreElim";
	}

	bool run(Module & module) override;
};
