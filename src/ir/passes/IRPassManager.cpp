// Small pass manager for module-level IR optimizations.

#include "IRPassManager.h"

#include "ir/include/Module.h"

bool IRPassManager::run(Module & module)
{
	for (auto & pass: passes) {
		if (pass == nullptr) {
			continue;
		}
		if (!pass->run(module)) {
			return false;
		}
	}
	return true;
}
