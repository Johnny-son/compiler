#include "MachineDCE.h"

#include "MachineCFG.h"
#include "MachineLiveness.h"

#include <utility>
#include <vector>

namespace {

bool hasPhysicalDef(const MachineInstr & inst)
{
	if (!inst.implicitDefs.empty()) {
		return true;
	}
	for (const auto & operand: inst.operands) {
		if (operand.kind == MachineOperandKind::PhysicalReg && operand.role == MachineOperandRole::Def) {
			return true;
		}
	}
	return false;
}

bool isRemovableOpcode(MachineOpcode opcode)
{
	switch (opcode) {
		case MachineOpcode::ADDI:
		case MachineOpcode::ADDIW:
		case MachineOpcode::ADD:
		case MachineOpcode::ADDW:
		case MachineOpcode::SUBW:
		case MachineOpcode::MUL:
		case MachineOpcode::MULW:
		case MachineOpcode::DIVW:
		case MachineOpcode::REMW:
		case MachineOpcode::SLLI:
		case MachineOpcode::AND:
		case MachineOpcode::OR:
		case MachineOpcode::XOR:
		case MachineOpcode::XORI:
		case MachineOpcode::ANDI:
		case MachineOpcode::SLT:
		case MachineOpcode::SEQZ:
		case MachineOpcode::SNEZ:
		case MachineOpcode::LI:
		case MachineOpcode::LA:
		case MachineOpcode::LA_STACK:
		case MachineOpcode::COPY:
			return true;
		default:
			return false;
	}
}

bool virtualDefsAreDead(const MachineInstr & inst, const MachineInstrLiveness & live)
{
	bool hasVirtualDef = false;
	for (const auto & operand: inst.operands) {
		if (operand.kind != MachineOperandKind::VirtualReg || operand.role != MachineOperandRole::Def) {
			continue;
		}
		hasVirtualDef = true;
		if (live.liveOut.find(operand.vreg) != live.liveOut.end()) {
			return false;
		}
	}
	return hasVirtualDef;
}

bool canRemove(const MachineInstr & inst, const MachineInstrLiveness & live)
{
	return isRemovableOpcode(inst.opcode) && !hasPhysicalDef(inst) && virtualDefsAreDead(inst, live);
}

bool runOnce(MachineFunction & function)
{
	MachineCFGBuilder cfgBuilder;
	cfgBuilder.run(function);
	MachineLivenessAnalysis livenessAnalysis;
	MachineLivenessResult liveness = livenessAnalysis.run(function);

	bool changed = false;
	for (MachineBlockIndex blockIndex = 0; blockIndex < function.blocks().size(); ++blockIndex) {
		auto & block = function.blocks()[blockIndex];
		std::vector<MachineInstr> kept;
		kept.reserve(block.instructions().size());
		bool blockChanged = false;
		for (std::size_t instIndex = 0; instIndex < block.instructions().size(); ++instIndex) {
			const auto & inst = block.instructions()[instIndex];
			if (canRemove(inst, liveness.instruction(blockIndex, instIndex))) {
				changed = true;
				blockChanged = true;
				continue;
			}
			kept.push_back(inst);
		}
		if (blockChanged) {
			block.instructions() = std::move(kept);
		}
	}
	return changed;
}

} // namespace

bool MachineDCE::run(MachineFunction & function) const
{
	bool changed = false;
	while (runOnce(function)) {
		changed = true;
	}
	return changed;
}
