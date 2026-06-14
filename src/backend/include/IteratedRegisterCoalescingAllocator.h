#pragma once

#include <string>

#include "FrameLayout.h"
#include "MachineIR.h"

class IteratedRegisterCoalescingAllocator {

public:
	explicit IteratedRegisterCoalescingAllocator(std::string debugTag = "");
	bool run(MachineFunction & function, FunctionFrameLayout & layout) const;

private:
	std::string debugTag;
};
