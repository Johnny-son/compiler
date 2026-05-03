#pragma once

#include "backend/include/MachineIR.h"

#include <string>

class MachineCFGBuilder {

public:
	void run(MachineFunction & function) const;

	static bool isTerminator(MachineOpcode opcode);
	static bool isBarrierTerminator(MachineOpcode opcode);

private:
	static bool targetLabel(const MachineInstr & inst, std::string & label);
	static void addEdge(MachineFunction & function, MachineBlockIndex from, MachineBlockIndex to);
};
