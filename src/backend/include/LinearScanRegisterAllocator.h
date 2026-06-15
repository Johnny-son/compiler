#pragma once

#include "FrameLayout.h"
#include "MachineIR.h"

class LinearScanRegisterAllocator {

public:
	bool run(MachineFunction & function, FunctionFrameLayout & layout) const;
};
