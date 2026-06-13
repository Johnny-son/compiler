#include "MachinePostRACleanup.h"

#include <utility>
#include <vector>

namespace {

bool samePhysicalReg(const MachineOperand & lhs, const MachineOperand & rhs)
{
	return lhs.kind == MachineOperandKind::PhysicalReg && rhs.kind == MachineOperandKind::PhysicalReg &&
		   lhs.preg == rhs.preg;
}

bool isRedundantCopy(const MachineInstr & inst)
{
	return inst.opcode == MachineOpcode::COPY && inst.operands.size() >= 2 &&
		   inst.operands[0].role == MachineOperandRole::Def && inst.operands[1].role == MachineOperandRole::Use &&
		   samePhysicalReg(inst.operands[0], inst.operands[1]);
}

bool isRedundantAddi(const MachineInstr & inst)
{
	return inst.opcode == MachineOpcode::ADDI && inst.operands.size() >= 3 &&
		   inst.operands[0].role == MachineOperandRole::Def && inst.operands[1].role == MachineOperandRole::Use &&
		   samePhysicalReg(inst.operands[0], inst.operands[1]) &&
		   inst.operands[2].kind == MachineOperandKind::Immediate && inst.operands[2].imm == 0;
}

bool isJumpToNextBlock(const MachineInstr & inst, const MachineBasicBlock * nextBlock)
{
	if (nextBlock == nullptr || inst.opcode != MachineOpcode::J || inst.operands.empty()) {
		return false;
	}

	const auto & target = inst.operands[0];
	return target.kind == MachineOperandKind::BlockLabel && target.text == nextBlock->label();
}

bool runOnBlock(MachineBasicBlock & block, const MachineBasicBlock * nextBlock)
{
	bool changed = false;
	std::vector<MachineInstr> kept;
	kept.reserve(block.instructions().size());

	for (std::size_t index = 0; index < block.instructions().size(); ++index) {
		const auto & inst = block.instructions()[index];
		const bool isLast = index + 1 == block.instructions().size();
		if (isRedundantCopy(inst) || isRedundantAddi(inst) || (isLast && isJumpToNextBlock(inst, nextBlock))) {
			changed = true;
			continue;
		}
		kept.push_back(inst);
	}

	if (changed) {
		block.instructions() = std::move(kept);
	}
	return changed;
}

} // namespace

bool MachinePostRACleanup::run(MachineFunction & function) const
{
	bool changed = false;
	for (MachineBlockIndex index = 0; index < function.blocks().size(); ++index) {
		const MachineBasicBlock * nextBlock = index + 1 < function.blocks().size() ? &function.blocks()[index + 1] : nullptr;
		changed = runOnBlock(function.blocks()[index], nextBlock) || changed;
	}
	return changed;
}
