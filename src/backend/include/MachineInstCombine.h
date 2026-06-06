#pragma once

#include "MachineIR.h"

class MachineInstCombine {

public:
	bool run(MachineFunction & function) const;
};
