#include "backend/include/MachineLegalizer.h"

#include <utility>
#include <vector>

MachineLegalizer::MachineLegalizer(const FunctionFrameLayout & layout) : frameLayout(layout)
{}

void MachineLegalizer::run(MachineFunction & function) const
{
	for (auto & block: function.blocks()) {
		std::vector<MachineInstr> legalized;
		legalized.reserve(block.instructions().size());

		for (const auto & inst: block.instructions()) {
			legalizeInstruction(function, legalized, inst);
		}

		block.instructions() = std::move(legalized);
	}
}

bool MachineLegalizer::isSigned12Bit(int64_t value)
{
	return value >= -2048 && value <= 2047;
}

bool MachineLegalizer::isMemoryOpcode(MachineOpcode opcode)
{
	return opcode == MachineOpcode::LW || opcode == MachineOpcode::LD || opcode == MachineOpcode::SW ||
		   opcode == MachineOpcode::SD;
}

void MachineLegalizer::legalizeInstruction(
	MachineFunction & function,
	std::vector<MachineInstr> & output,
	const MachineInstr & inst) const
{
	if (inst.opcode == MachineOpcode::ADDI) {
		legalizeAddi(function, output, inst);
		return;
	}

	if (inst.opcode == MachineOpcode::LA_STACK) {
		legalizeLaStack(function, output, inst);
		return;
	}

	if (isMemoryOpcode(inst.opcode)) {
		legalizeMemoryInstruction(function, output, inst);
		return;
	}

	output.push_back(inst);
}

void MachineLegalizer::legalizeAddi(
	MachineFunction & function,
	std::vector<MachineInstr> & output,
	const MachineInstr & inst) const
{
	if (inst.operands.size() < 3 || inst.operands[2].kind != MachineOperandKind::Immediate ||
		isSigned12Bit(inst.operands[2].imm)) {
		output.push_back(inst);
		return;
	}

	MachineOperand imm = materializeImmediate(function, output, inst.operands[2].imm);
	MachineInstr add = inst;
	add.opcode = MachineOpcode::ADD;
	add.operands[2] = imm;
	output.push_back(add);
}

void MachineLegalizer::legalizeLaStack(
	MachineFunction & function,
	std::vector<MachineInstr> & output,
	const MachineInstr & inst) const
{
	if (inst.operands.size() < 2 || inst.operands[1].kind != MachineOperandKind::StackSlot) {
		output.push_back(inst);
		return;
	}

	const int64_t offset = stackSlotOffset(inst.operands[1].stackValue);
	if (isSigned12Bit(offset)) {
		output.push_back(inst);
		return;
	}

	MachineOperand addr = materializeAddress(
		function,
		output,
		MachineOperand::pregUse(PhysicalReg::FP),
		offset);
	output.push_back(MachineInstr::make(MachineOpcode::COPY, {inst.operands[0], addr}));
}

void MachineLegalizer::legalizeMemoryInstruction(
	MachineFunction & function,
	std::vector<MachineInstr> & output,
	const MachineInstr & inst) const
{
	MachineInstr legalized = inst;
	for (auto & operand: legalized.operands) {
		if (operand.kind == MachineOperandKind::Memory || operand.kind == MachineOperandKind::StackSlot) {
			operand = legalizeMemoryOperand(function, output, operand);
		}
	}
	output.push_back(legalized);
}

MachineOperand MachineLegalizer::materializeImmediate(
	MachineFunction & function,
	std::vector<MachineInstr> & output,
	int64_t value) const
{
	MachineOperand tmp = MachineOperand::vregDef(function.createVirtualReg(RegisterClass::GPR));
	output.push_back(MachineInstr::make(MachineOpcode::LI, {tmp, MachineOperand::immValue(value)}));
	return tmp.asUse();
}

MachineOperand MachineLegalizer::materializeAddress(
	MachineFunction & function,
	std::vector<MachineInstr> & output,
	const MachineOperand & base,
	int64_t offset) const
{
	MachineOperand offsetReg = materializeImmediate(function, output, offset);
	MachineOperand address = MachineOperand::vregDef(function.createVirtualReg(RegisterClass::GPR));
	output.push_back(MachineInstr::make(MachineOpcode::ADD, {address, base.asUse(), offsetReg}));
	return address.asUse();
}

MachineOperand MachineLegalizer::legalizeMemoryOperand(
	MachineFunction & function,
	std::vector<MachineInstr> & output,
	const MachineOperand & operand) const
{
	int64_t offset = operand.memoryOffset;
	MachineOperand base;

	if (operand.kind == MachineOperandKind::StackSlot) {
		offset = stackSlotOffset(operand.stackValue);
		base = MachineOperand::pregUse(PhysicalReg::FP);
	} else if (operand.memoryBaseIsPhysical) {
		base = MachineOperand::pregUse(operand.memoryBasePreg);
	} else {
		base = MachineOperand::vregUse(operand.memoryBaseVReg, operand.regClass);
	}

	if (isSigned12Bit(offset)) {
		if (operand.kind == MachineOperandKind::StackSlot) {
			return operand;
		}
		return operand;
	}

	MachineOperand address = materializeAddress(function, output, base, offset);
	return MachineOperand::memVReg(address.vreg, 0);
}

int64_t MachineLegalizer::stackSlotOffset(Value * value) const
{
	const auto * slot = frameLayout.slotOf(value);
	if (slot == nullptr) {
		return 0;
	}
	return slot->offset;
}
