#pragma once

#include "FrameLayout.h"
#include "MachineIR.h"

class IteratedRegisterCoalescingAllocator {

public:
	bool run(MachineFunction & function, FunctionFrameLayout & layout) const;
};
