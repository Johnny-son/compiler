// Minimal IR pass interface.

#pragma once

#include <string>

class Module;

class IRPass {
public:
	virtual ~IRPass() = default;

	virtual std::string name() const = 0;
	virtual bool run(Module & module) = 0;
};
