#include "backend/include/NaiveRegisterAllocator.h"

namespace {

MachineOperand rewriteOperand(const MachineOperand & operand, const NaiveRegisterAllocator & allocator)
{
	MachineOperand rewritten = operand;
	if (operand.kind == MachineOperandKind::VirtualReg) {
		rewritten.kind = MachineOperandKind::PhysicalReg;
		rewritten.preg = allocator.assignVirtualReg(operand.vreg);
		rewritten.vreg = -1;
		return rewritten;
	}

	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		rewritten.memoryBaseIsPhysical = true;
		rewritten.memoryBasePreg = allocator.assignVirtualReg(operand.memoryBaseVReg);
		rewritten.memoryBaseVReg = -1;
	}

	return rewritten;
}

} // namespace

MachineFunction NaiveRegisterAllocator::run(const MachineFunction & function) const
{
	MachineFunction allocated(function.name());
	for (const auto & block: function.blocks()) {
		auto & newBlock = allocated.createBlock(block.label());
		for (const auto & inst: block.instructions()) {
			MachineInstr rewritten = inst;
			rewritten.operands.clear();
			rewritten.operands.reserve(inst.operands.size());
			for (const auto & operand: inst.operands) {
				rewritten.operands.push_back(rewriteOperand(operand, *this));
			}
			newBlock.emit(rewritten);
		}
	}
	return allocated;
}

PhysicalReg NaiveRegisterAllocator::assignVirtualReg(int32_t vreg) const
{
	static constexpr PhysicalReg temps[] = {
		PhysicalReg::T0,
		PhysicalReg::T1,
		PhysicalReg::T2,
		PhysicalReg::T3,
		PhysicalReg::T4,
	};

	if (vreg < 0) {
		return PhysicalReg::T0;
	}

	return temps[vreg % (sizeof(temps) / sizeof(temps[0]))];
}
