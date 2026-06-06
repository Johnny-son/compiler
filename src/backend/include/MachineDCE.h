#pragma once

#include "MachineIR.h"

class MachineDCE {

public:
	bool run(MachineFunction & function) const;
};
