#pragma once

#include "FrameLayout.h"
#include "MachineIR.h"

class GraphColoringRegisterAllocator {

public:
	bool run(MachineFunction & function, FunctionFrameLayout & layout) const;
};
