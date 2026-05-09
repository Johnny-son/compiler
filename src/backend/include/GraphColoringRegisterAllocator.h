#pragma once

#include "backend/include/FrameLayout.h"
#include "backend/include/MachineIR.h"

class GraphColoringRegisterAllocator {

public:
	bool run(MachineFunction & function, FunctionFrameLayout & layout) const;
};
