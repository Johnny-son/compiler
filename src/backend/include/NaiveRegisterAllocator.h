#pragma once

#include "backend/include/MachineIR.h"

class NaiveRegisterAllocator {

public:
	MachineFunction run(const MachineFunction & function) const;
	PhysicalReg assignVirtualReg(int32_t vreg) const;
};
