#include "MachineAsmLowering.h"

#include <string>
#include <utility>

namespace {

bool isFPRName(const std::string & name)
{
	return name.rfind("fa", 0) == 0 || name.rfind("ft", 0) == 0 || name.rfind("fs", 0) == 0;
}

} // namespace

MachineAsmLowering::MachineAsmLowering(const MachineFunction & function, const FunctionFrameLayout & layout)
	: function(function), frameLayout(layout)
{}

AsmFunction MachineAsmLowering::run() const
{
	AsmFunction asmFunction(function.name());

	bool firstBlock = true;
	for (const auto & block: function.blocks()) {
		if (!firstBlock) {
			asmFunction.emitLabel(block.label());
		}
		firstBlock = false;

		for (const auto & inst: block.instructions()) {
			asmFunction.emit(lowerInstruction(inst));
		}
	}

	return asmFunction;
}

AsmInstruction MachineAsmLowering::lowerInstruction(const MachineInstr & inst) const
{
	if (inst.opcode == MachineOpcode::COPY) {
		const auto dst = lowerOperand(inst.operands[0]);
		const auto src = lowerOperand(inst.operands[1]);
		if (dst.kind == AsmOperandKind::Register && src.kind == AsmOperandKind::Register &&
			(isFPRName(dst.text) || isFPRName(src.text))) {
			return AsmInstruction::makeOp("fmv.s", {dst, src});
		}
		return AsmInstruction::makeOp("mv", {dst, src});
	}

	if (inst.opcode == MachineOpcode::LA_STACK) {
		return AsmInstruction::makeOp(
			"addi",
			{lowerOperand(inst.operands[0]),
			 AsmOperand::reg(TargetRegisterInfo::name(PhysicalReg::FP)),
			 AsmOperand::immValue(stackSlotOffset(inst.operands[1].stackValue))});
	}

	std::vector<AsmOperand> operands;
	operands.reserve(inst.operands.size());

	for (std::size_t index = 0; index < inst.operands.size(); ++index) {
		const auto & operand = inst.operands[index];
		if (operand.kind == MachineOperandKind::StackSlot) {
			operands.push_back(lowerStackSlot(operand.stackValue));
			continue;
		}
		if (operand.kind == MachineOperandKind::SpillSlot) {
			operands.push_back(lowerSpillSlot(operand.spillSlot));
			continue;
		}
		operands.push_back(lowerOperand(operand));
	}

	return AsmInstruction::makeOp(opcodeName(inst.opcode), std::move(operands));
}

AsmOperand MachineAsmLowering::lowerOperand(const MachineOperand & operand) const
{
	switch (operand.kind) {
		case MachineOperandKind::PhysicalReg:
			return AsmOperand::reg(TargetRegisterInfo::name(operand.preg));
		case MachineOperandKind::Immediate:
			return AsmOperand::immValue(operand.imm);
		case MachineOperandKind::StackSlot:
			return lowerStackSlot(operand.stackValue);
		case MachineOperandKind::SpillSlot:
			return lowerSpillSlot(operand.spillSlot);
		case MachineOperandKind::Memory:
			return lowerMemoryOperand(operand);
		case MachineOperandKind::BlockLabel:
			return AsmOperand::label(operand.text);
		case MachineOperandKind::GlobalSymbol:
		case MachineOperandKind::FunctionSymbol:
			return AsmOperand::symbol(operand.text);
		case MachineOperandKind::VirtualReg:
			return AsmOperand::reg("%v" + std::to_string(operand.vreg));
	}
	return AsmOperand::immValue(0);
}

AsmOperand MachineAsmLowering::lowerMemoryOperand(const MachineOperand & operand) const
{
	if (!operand.memoryBaseIsPhysical) {
		return AsmOperand::mem("%v" + std::to_string(operand.memoryBaseVReg), operand.memoryOffset);
	}
	return AsmOperand::mem(TargetRegisterInfo::name(operand.memoryBasePreg), operand.memoryOffset);
}

AsmOperand MachineAsmLowering::lowerStackSlot(Value * value) const
{
	return AsmOperand::mem(TargetRegisterInfo::name(PhysicalReg::FP), stackSlotOffset(value));
}

AsmOperand MachineAsmLowering::lowerSpillSlot(int32_t id) const
{
	return AsmOperand::mem(TargetRegisterInfo::name(PhysicalReg::FP), spillSlotOffset(id));
}

int64_t MachineAsmLowering::stackSlotOffset(Value * value) const
{
	const auto * slot = frameLayout.slotOf(value);
	if (slot == nullptr) {
		return 0;
	}

	return slot->offset;
}

int64_t MachineAsmLowering::spillSlotOffset(int32_t id) const
{
	const auto * slot = frameLayout.spillSlot(id);
	if (slot == nullptr) {
		return 0;
	}

	return slot->offset;
}

std::string MachineAsmLowering::opcodeName(MachineOpcode opcode) const
{
	switch (opcode) {
		case MachineOpcode::ADDI:
			return "addi";
		case MachineOpcode::ADDIW:
			return "addiw";
		case MachineOpcode::ADD:
			return "add";
		case MachineOpcode::ADDW:
			return "addw";
		case MachineOpcode::SUBW:
			return "subw";
		case MachineOpcode::MUL:
			return "mul";
		case MachineOpcode::MULW:
			return "mulw";
		case MachineOpcode::DIVW:
			return "divw";
		case MachineOpcode::REMW:
			return "remw";
		case MachineOpcode::SLLI:
			return "slli";
		case MachineOpcode::XOR:
			return "xor";
		case MachineOpcode::SLT:
			return "slt";
		case MachineOpcode::XORI:
			return "xori";
		case MachineOpcode::ANDI:
			return "andi";
		case MachineOpcode::SEQZ:
			return "seqz";
		case MachineOpcode::SNEZ:
			return "snez";
		case MachineOpcode::LI:
			return "li";
		case MachineOpcode::LA:
			return "la";
		case MachineOpcode::LW:
			return "lw";
		case MachineOpcode::LD:
			return "ld";
		case MachineOpcode::FLW:
			return "flw";
		case MachineOpcode::SW:
			return "sw";
		case MachineOpcode::SD:
			return "sd";
		case MachineOpcode::FSW:
			return "fsw";
		case MachineOpcode::FADD_S:
			return "fadd.s";
		case MachineOpcode::FSUB_S:
			return "fsub.s";
		case MachineOpcode::FMUL_S:
			return "fmul.s";
		case MachineOpcode::FDIV_S:
			return "fdiv.s";
		case MachineOpcode::FEQ_S:
			return "feq.s";
		case MachineOpcode::FLT_S:
			return "flt.s";
		case MachineOpcode::FLE_S:
			return "fle.s";
		case MachineOpcode::FCVT_S_W:
			return "fcvt.s.w";
		case MachineOpcode::FCVT_W_S:
			return "fcvt.w.s";
		case MachineOpcode::FMV_W_X:
			return "fmv.w.x";
		case MachineOpcode::FMV_X_W:
			return "fmv.x.w";
		case MachineOpcode::CALL:
			return "call";
		case MachineOpcode::J:
			return "j";
		case MachineOpcode::BEQ:
			return "beq";
		case MachineOpcode::BNE:
			return "bne";
		case MachineOpcode::BLT:
			return "blt";
		case MachineOpcode::BGE:
			return "bge";
		case MachineOpcode::BNEZ:
			return "bnez";
		case MachineOpcode::RET:
			return "ret";
		case MachineOpcode::COPY:
		case MachineOpcode::LA_STACK:
			break;
	}
	return "unknown";
}
