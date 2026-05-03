#pragma once

#include "backend/include/Asm.h"
#include "backend/include/FrameLayout.h"
#include "backend/include/MachineIR.h"

class MachineAsmLowering {

public:
	MachineAsmLowering(const MachineFunction & function, const FunctionFrameLayout & layout);

	AsmFunction run() const;

private:
	AsmInstruction lowerInstruction(const MachineInstr & inst) const;
	AsmOperand lowerOperand(const MachineOperand & operand) const;
	AsmOperand lowerMemoryOperand(const MachineOperand & operand) const;
	AsmOperand lowerStackSlot(Value * value) const;
	int64_t stackSlotOffset(Value * value) const;
	std::string opcodeName(MachineOpcode opcode) const;

private:
	const MachineFunction & function;
	const FunctionFrameLayout & frameLayout;
};
