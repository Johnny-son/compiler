#include "backend/include/MachineCFG.h"

#include <unordered_map>

void MachineCFGBuilder::run(MachineFunction & function) const
{
	auto & blocks = function.blocks();
	for (auto & block: blocks) {
		block.clearCFGLinks();
	}

	std::unordered_map<std::string, MachineBlockIndex> labelToIndex;
	for (MachineBlockIndex index = 0; index < blocks.size(); ++index) {
		labelToIndex[blocks[index].label()] = index;
	}

	for (MachineBlockIndex index = 0; index < blocks.size(); ++index) {
		const auto & insts = blocks[index].instructions();
		bool hasBarrier = false;

		for (auto instIndex = insts.size(); instIndex > 0; --instIndex) {
			const auto & inst = insts[instIndex - 1];
			if (!isTerminator(inst.opcode)) {
				break;
			}

			if (isBarrierTerminator(inst.opcode)) {
				hasBarrier = true;
			}

			std::string label;
			if (!targetLabel(inst, label)) {
				continue;
			}

			auto target = labelToIndex.find(label);
			if (target != labelToIndex.end()) {
				addEdge(function, index, target->second);
			}
		}

		if (!hasBarrier && index + 1 < blocks.size()) {
			addEdge(function, index, index + 1);
		}
	}
}

bool MachineCFGBuilder::isTerminator(MachineOpcode opcode)
{
	return opcode == MachineOpcode::J || opcode == MachineOpcode::BNEZ || opcode == MachineOpcode::RET;
}

bool MachineCFGBuilder::isBarrierTerminator(MachineOpcode opcode)
{
	return opcode == MachineOpcode::J || opcode == MachineOpcode::RET;
}

bool MachineCFGBuilder::targetLabel(const MachineInstr & inst, std::string & label)
{
	if (inst.opcode == MachineOpcode::J && inst.operands.size() >= 1 &&
		inst.operands[0].kind == MachineOperandKind::BlockLabel) {
		label = inst.operands[0].text;
		return true;
	}

	if (inst.opcode == MachineOpcode::BNEZ && inst.operands.size() >= 2 &&
		inst.operands[1].kind == MachineOperandKind::BlockLabel) {
		label = inst.operands[1].text;
		return true;
	}

	return false;
}

void MachineCFGBuilder::addEdge(MachineFunction & function, MachineBlockIndex from, MachineBlockIndex to)
{
	auto & blocks = function.blocks();
	if (from >= blocks.size() || to >= blocks.size()) {
		return;
	}

	blocks[from].addSuccessor(to);
	blocks[to].addPredecessor(from);
}
