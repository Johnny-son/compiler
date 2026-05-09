#pragma once

#include "backend/include/FrameLayout.h"
#include "backend/include/MachineIR.h"

class MachineLegalizer {

public:
	explicit MachineLegalizer(const FunctionFrameLayout & layout);

	void run(MachineFunction & function, bool skipFrameSetup = false) const;
	static bool isFrameSetupInstruction(const MachineInstr & inst);

private:
	static bool isSigned12Bit(int64_t value);
	static bool isMemoryOpcode(MachineOpcode opcode);

	void legalizeInstruction(
		MachineFunction & function,
		std::vector<MachineInstr> & output,
		const MachineInstr & inst) const;
	void legalizeAddi(MachineFunction & function, std::vector<MachineInstr> & output, const MachineInstr & inst) const;
	void legalizeLaStack(
		MachineFunction & function,
		std::vector<MachineInstr> & output,
		const MachineInstr & inst) const;
	void legalizeMemoryInstruction(
		MachineFunction & function,
		std::vector<MachineInstr> & output,
		const MachineInstr & inst) const;

	MachineOperand materializeImmediate(
		MachineFunction & function,
		std::vector<MachineInstr> & output,
		int64_t value) const;
	MachineOperand materializeAddress(
		MachineFunction & function,
		std::vector<MachineInstr> & output,
		const MachineOperand & base,
		int64_t offset) const;
	MachineOperand legalizeMemoryOperand(
		MachineFunction & function,
		std::vector<MachineInstr> & output,
		const MachineOperand & operand) const;

	int64_t frameOperandOffset(const MachineOperand & operand) const;
	int64_t stackSlotOffset(Value * value) const;
	int64_t spillSlotOffset(int32_t id) const;

private:
	const FunctionFrameLayout & frameLayout;
};
