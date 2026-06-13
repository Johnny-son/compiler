#pragma once

#include "MachineIR.h"

class MachinePostRACleanup {

public:
	bool run(MachineFunction & function) const;
};
