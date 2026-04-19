#pragma once

#include <string>

class Module;

class BackendDriver {

public:
	bool run(Module * module, const std::string & outputFile) const;
};
