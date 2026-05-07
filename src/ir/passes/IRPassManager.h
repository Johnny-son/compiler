// Small pass manager for module-level IR optimizations.

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "IRPass.h"

class Module;

class IRPassManager {
public:
	template<typename PassT, typename... Args>
	PassT * addPass(Args &&... args)
	{
		auto pass = std::make_unique<PassT>(std::forward<Args>(args)...);
		auto * raw = pass.get();
		passes.push_back(std::move(pass));
		return raw;
	}

	bool run(Module & module);

private:
	std::vector<std::unique_ptr<IRPass>> passes;
};
