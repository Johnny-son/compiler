#include "backend/include/MachineLiveness.h"

#include <sstream>

namespace {

void insertAll(VRegSet & dst, const VRegSet & src)
{
	dst.insert(src.begin(), src.end());
}

VRegSet setDifference(const VRegSet & lhs, const VRegSet & rhs)
{
	VRegSet result;
	for (int32_t value: lhs) {
		if (rhs.find(value) == rhs.end()) {
			result.insert(value);
		}
	}
	return result;
}

void appendSet(std::ostringstream & out, const VRegSet & values)
{
	for (int32_t value: values) {
		out << " %v" << value;
	}
}

void collectOperandUseDef(const MachineOperand & operand, MachineInstrLiveness & info)
{
	if (operand.kind == MachineOperandKind::VirtualReg) {
		if (operand.role == MachineOperandRole::Use) {
			info.use.insert(operand.vreg);
		} else if (operand.role == MachineOperandRole::Def) {
			info.def.insert(operand.vreg);
		}
		return;
	}

	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		info.use.insert(operand.memoryBaseVReg);
	}
}

} // namespace

const std::vector<MachineBlockLiveness> & MachineLivenessResult::blocks() const
{
	return blockInfos;
}

const MachineBlockLiveness & MachineLivenessResult::block(MachineBlockIndex index) const
{
	return blockInfos[index];
}

const MachineInstrLiveness & MachineLivenessResult::instruction(MachineBlockIndex blockIndex, std::size_t instIndex) const
{
	return blockInfos[blockIndex].instructions[instIndex];
}

std::string MachineLivenessResult::toString(const MachineFunction & function) const
{
	std::ostringstream out;
	out << "liveness " << function.name() << "\n";

	for (MachineBlockIndex blockIndex = 0; blockIndex < blockInfos.size(); ++blockIndex) {
		const auto & blockInfo = blockInfos[blockIndex];
		out << function.blocks()[blockIndex].label() << ":\n";

		out << "  use:";
		appendSet(out, blockInfo.use);
		out << "\n";

		out << "  def:";
		appendSet(out, blockInfo.def);
		out << "\n";

		out << "  live-in:";
		appendSet(out, blockInfo.liveIn);
		out << "\n";

		out << "  live-out:";
		appendSet(out, blockInfo.liveOut);
		out << "\n";
	}

	return out.str();
}

MachineLivenessResult MachineLivenessAnalysis::run(const MachineFunction & function) const
{
	MachineLivenessResult result;
	result.blockInfos.resize(function.blocks().size());

	for (MachineBlockIndex blockIndex = 0; blockIndex < function.blocks().size(); ++blockIndex) {
		collectBlockUseDef(function.blocks()[blockIndex], result.blockInfos[blockIndex]);
	}

	solveBlockLiveness(function, result);
	computeInstructionLiveness(function, result);

	return result;
}

MachineInstrLiveness MachineLivenessAnalysis::collectUseDef(const MachineInstr & inst)
{
	MachineInstrLiveness info;
	for (const auto & operand: inst.operands) {
		collectOperandUseDef(operand, info);
	}
	return info;
}

void MachineLivenessAnalysis::collectBlockUseDef(const MachineBasicBlock & block, MachineBlockLiveness & blockInfo)
{
	blockInfo.instructions.clear();
	blockInfo.instructions.reserve(block.instructions().size());
	blockInfo.use.clear();
	blockInfo.def.clear();
	blockInfo.liveIn.clear();
	blockInfo.liveOut.clear();

	for (const auto & inst: block.instructions()) {
		MachineInstrLiveness instInfo = collectUseDef(inst);

		for (int32_t use: instInfo.use) {
			if (blockInfo.def.find(use) == blockInfo.def.end()) {
				blockInfo.use.insert(use);
			}
		}

		insertAll(blockInfo.def, instInfo.def);
		blockInfo.instructions.push_back(std::move(instInfo));
	}
}

void MachineLivenessAnalysis::solveBlockLiveness(const MachineFunction & function, MachineLivenessResult & result)
{
	bool changed = true;
	while (changed) {
		changed = false;

		for (auto blockIndex = result.blockInfos.size(); blockIndex > 0; --blockIndex) {
			MachineBlockIndex index = blockIndex - 1;
			auto & blockInfo = result.blockInfos[index];

			VRegSet oldLiveIn = blockInfo.liveIn;
			VRegSet oldLiveOut = blockInfo.liveOut;

			blockInfo.liveOut.clear();
			for (MachineBlockIndex succ: function.blocks()[index].successors()) {
				insertAll(blockInfo.liveOut, result.blockInfos[succ].liveIn);
			}

			blockInfo.liveIn = blockInfo.use;
			insertAll(blockInfo.liveIn, setDifference(blockInfo.liveOut, blockInfo.def));

			if (blockInfo.liveIn != oldLiveIn || blockInfo.liveOut != oldLiveOut) {
				changed = true;
			}
		}
	}
}

void MachineLivenessAnalysis::computeInstructionLiveness(const MachineFunction &, MachineLivenessResult & result)
{
	for (MachineBlockIndex blockIndex = 0; blockIndex < result.blockInfos.size(); ++blockIndex) {
		auto & blockInfo = result.blockInfos[blockIndex];
		VRegSet live = blockInfo.liveOut;

		for (auto instIndex = blockInfo.instructions.size(); instIndex > 0; --instIndex) {
			auto & instInfo = blockInfo.instructions[instIndex - 1];
			instInfo.liveOut = live;

			live = instInfo.use;
			insertAll(live, setDifference(instInfo.liveOut, instInfo.def));

			instInfo.liveIn = live;
		}
	}
}
