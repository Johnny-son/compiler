#include "MachineLegalizer.h"

#include <utility>
#include <vector>

MachineLegalizer::MachineLegalizer(const FunctionFrameLayout & layout) : frameLayout(layout)
{}

void MachineLegalizer::run(MachineFunction & function, bool skipFrameSetup) const
{
	for (MachineBlockIndex blockIndex = 0; blockIndex < function.blocks().size(); ++blockIndex) {
		auto & block = function.blocks()[blockIndex];
		std::vector<MachineInstr> legalized;
		legalized.reserve(block.instructions().size());

		for (const auto & inst: block.instructions()) {
			if (skipFrameSetup && blockIndex == 0 && isFrameSetupInstruction(inst)) {
				legalized.push_back(inst);
				continue;
			}
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
		   opcode == MachineOpcode::SD || opcode == MachineOpcode::FLW || opcode == MachineOpcode::FSW;
}

bool MachineLegalizer::isFrameSetupInstruction(const MachineInstr & inst)
{
	if (inst.opcode == MachineOpcode::ADDI && inst.operands.size() >= 3) {
		return inst.operands[0].kind == MachineOperandKind::PhysicalReg &&
			   inst.operands[1].kind == MachineOperandKind::PhysicalReg &&
			   ((inst.operands[0].preg == PhysicalReg::SP && inst.operands[1].preg == PhysicalReg::SP &&
			     inst.operands[2].kind == MachineOperandKind::Immediate && inst.operands[2].imm <= 0) ||
			    (inst.operands[0].preg == PhysicalReg::FP && inst.operands[1].preg == PhysicalReg::SP));
	}

	if ((inst.opcode == MachineOpcode::SD || inst.opcode == MachineOpcode::SW) && inst.operands.size() >= 2 &&
		inst.operands[0].kind == MachineOperandKind::PhysicalReg &&
		(inst.operands[0].preg == PhysicalReg::RA || inst.operands[0].preg == PhysicalReg::FP) &&
		inst.operands[1].kind == MachineOperandKind::Memory && inst.operands[1].memoryBaseIsPhysical &&
		inst.operands[1].memoryBasePreg == PhysicalReg::SP) {
		return true;
	}

	return false;
}

void MachineLegalizer::legalizeInstruction(
	MachineFunction & function,
	std::vector<MachineInstr> & output,
	const MachineInstr & inst) const
{
	if (isFrameSetupInstruction(inst)) {
		legalizeFrameSetupInstruction(output, inst);
		return;
	}

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

void MachineLegalizer::legalizeFrameSetupInstruction(
	std::vector<MachineInstr> & output,
	const MachineInstr & inst) const
{
	if (inst.opcode == MachineOpcode::ADDI) {
		if (inst.operands.size() < 3 || inst.operands[2].kind != MachineOperandKind::Immediate ||
			isSigned12Bit(inst.operands[2].imm)) {
			output.push_back(inst);
			return;
		}

		MachineOperand imm = materializeImmediateInScratch(output, inst.operands[2].imm);
		MachineInstr add = inst;
		add.opcode = MachineOpcode::ADD;
		add.operands[2] = imm;
		output.push_back(add);
		return;
	}

	if (isMemoryOpcode(inst.opcode)) {
		MachineInstr legalized = inst;
		for (auto & operand: legalized.operands) {
			if (operand.kind != MachineOperandKind::Memory || !operand.memoryBaseIsPhysical ||
				isSigned12Bit(operand.memoryOffset)) {
				continue;
			}
			MachineOperand address = materializeAddressInScratch(
				output,
				MachineOperand::pregUse(operand.memoryBasePreg),
				operand.memoryOffset);
			operand = MachineOperand::mem(address.preg, 0);
		}
		output.push_back(legalized);
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
	bool hasPhysicalRegOperand = false;
	for (const auto & operand: legalized.operands) {
		if (operand.kind == MachineOperandKind::PhysicalReg) {
			hasPhysicalRegOperand = true;
			break;
		}
	}

	for (auto & operand: legalized.operands) {
		if (operand.kind == MachineOperandKind::Memory || operand.kind == MachineOperandKind::StackSlot ||
			operand.kind == MachineOperandKind::SpillSlot) {
			if (hasPhysicalRegOperand &&
				(operand.kind == MachineOperandKind::StackSlot || operand.kind == MachineOperandKind::SpillSlot)) {
				const int64_t offset = frameOperandOffset(operand);
				if (!isSigned12Bit(offset)) {
					MachineOperand address = materializeAddressInScratch(
						output,
						MachineOperand::pregUse(PhysicalReg::FP),
						offset);
					operand = MachineOperand::mem(address.preg, 0);
					continue;
				}
			}
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

MachineOperand MachineLegalizer::materializeImmediateInScratch(
	std::vector<MachineInstr> & output,
	int64_t value) const
{
	output.push_back(MachineInstr::make(
		MachineOpcode::LI,
		{MachineOperand::pregDef(PhysicalReg::T0), MachineOperand::immValue(value)}));
	return MachineOperand::pregUse(PhysicalReg::T0);
}

MachineOperand MachineLegalizer::materializeAddressInScratch(
	std::vector<MachineInstr> & output,
	const MachineOperand & base,
	int64_t offset) const
{
	MachineOperand offsetReg = materializeImmediateInScratch(output, offset);
	output.push_back(MachineInstr::make(
		MachineOpcode::ADD,
		{MachineOperand::pregDef(PhysicalReg::T0), base.asUse(), offsetReg}));
	return MachineOperand::pregUse(PhysicalReg::T0);
}

MachineOperand MachineLegalizer::legalizeMemoryOperand(
	MachineFunction & function,
	std::vector<MachineInstr> & output,
	const MachineOperand & operand) const
{
	int64_t offset = operand.memoryOffset;
	MachineOperand base;

	if (operand.kind == MachineOperandKind::StackSlot || operand.kind == MachineOperandKind::SpillSlot) {
		offset = frameOperandOffset(operand);
		base = MachineOperand::pregUse(PhysicalReg::FP);
	} else if (operand.memoryBaseIsPhysical) {
		base = MachineOperand::pregUse(operand.memoryBasePreg);
	} else {
		base = MachineOperand::vregUse(operand.memoryBaseVReg, operand.regClass);
	}

	if (isSigned12Bit(offset)) {
		if (operand.kind == MachineOperandKind::StackSlot || operand.kind == MachineOperandKind::SpillSlot) {
			return operand;
		}
		return operand;
	}

	MachineOperand address = materializeAddress(function, output, base, offset);
	return MachineOperand::memVReg(address.vreg, 0);
}

int64_t MachineLegalizer::frameOperandOffset(const MachineOperand & operand) const
{
	if (operand.kind == MachineOperandKind::StackSlot) {
		return stackSlotOffset(operand.stackValue);
	}
	if (operand.kind == MachineOperandKind::SpillSlot) {
		return spillSlotOffset(operand.spillSlot);
	}
	return 0;
}

int64_t MachineLegalizer::stackSlotOffset(Value * value) const
{
	const auto * slot = frameLayout.slotOf(value);
	if (slot == nullptr) {
		return 0;
	}
	return slot->offset;
}

int64_t MachineLegalizer::spillSlotOffset(int32_t id) const
{
	const auto * slot = frameLayout.spillSlot(id);
	if (slot == nullptr) {
		return 0;
	}
	return slot->offset;
}
