#pragma once

#include "MachineIR.h"

class MachineLocalCSE {

public:
	bool run(MachineFunction & function) const;
};
