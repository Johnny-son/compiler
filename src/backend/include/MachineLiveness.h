#pragma once

#include "backend/include/MachineIR.h"

#include <set>
#include <string>
#include <vector>

using VRegSet = std::set<int32_t>;

struct MachineInstrLiveness {
	VRegSet use;
	VRegSet def;
	VRegSet liveIn;
	VRegSet liveOut;
};

struct MachineBlockLiveness {
	VRegSet use;
	VRegSet def;
	VRegSet liveIn;
	VRegSet liveOut;
	std::vector<MachineInstrLiveness> instructions;
};

class MachineLivenessResult {

public:
	[[nodiscard]] const std::vector<MachineBlockLiveness> & blocks() const;
	[[nodiscard]] const MachineBlockLiveness & block(MachineBlockIndex index) const;
	[[nodiscard]] const MachineInstrLiveness & instruction(MachineBlockIndex blockIndex, std::size_t instIndex) const;
	[[nodiscard]] std::string toString(const MachineFunction & function) const;

private:
	friend class MachineLivenessAnalysis;

	std::vector<MachineBlockLiveness> blockInfos;
};

class MachineLivenessAnalysis {

public:
	[[nodiscard]] MachineLivenessResult run(const MachineFunction & function) const;

	static MachineInstrLiveness collectUseDef(const MachineInstr & inst);

private:
	static void collectBlockUseDef(const MachineBasicBlock & block, MachineBlockLiveness & blockInfo);
	static void solveBlockLiveness(const MachineFunction & function, MachineLivenessResult & result);
	static void computeInstructionLiveness(const MachineFunction & function, MachineLivenessResult & result);
};
