#include "LinearScanRegisterAllocator.h"

#include "MachineCFG.h"
#include "MachineLegalizer.h"
#include "MachineLiveness.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

const PhysicalReg gprRegs[] = {
	PhysicalReg::S1,
	PhysicalReg::S2,
	PhysicalReg::S3,
	PhysicalReg::S4,
	PhysicalReg::S5,
	PhysicalReg::S6,
	PhysicalReg::S7,
	PhysicalReg::S8,
	PhysicalReg::S9,
	PhysicalReg::S10,
	PhysicalReg::S11,
};

const PhysicalReg callerSavedGPRs[] = {
	PhysicalReg::T1,
	PhysicalReg::T2,
	PhysicalReg::T3,
	PhysicalReg::T4,
	PhysicalReg::T5,
	PhysicalReg::T6,
};

const PhysicalReg fprRegs[] = {
	PhysicalReg::FS0,
	PhysicalReg::FS1,
	PhysicalReg::FS2,
	PhysicalReg::FS3,
	PhysicalReg::FS4,
	PhysicalReg::FS5,
	PhysicalReg::FS6,
	PhysicalReg::FS7,
	PhysicalReg::FS8,
	PhysicalReg::FS9,
	PhysicalReg::FS10,
	PhysicalReg::FS11,
};

const PhysicalReg callerSavedFPRs[] = {
	PhysicalReg::FT0,
	PhysicalReg::FT1,
	PhysicalReg::FT2,
	PhysicalReg::FT3,
	PhysicalReg::FT4,
	PhysicalReg::FT5,
	PhysicalReg::FT6,
	PhysicalReg::FT7,
	PhysicalReg::FT8,
	PhysicalReg::FT9,
	PhysicalReg::FT10,
	PhysicalReg::FT11,
};

const PhysicalReg gprScratch[] = {
	PhysicalReg::T1,
	PhysicalReg::T2,
	PhysicalReg::T3,
	PhysicalReg::T4,
};

const PhysicalReg fprScratch[] = {
	PhysicalReg::FT0,
	PhysicalReg::FT1,
	PhysicalReg::FT2,
	PhysicalReg::FT3,
};

struct Interval {
	int32_t vreg = -1;
	RegisterClass regClass = RegisterClass::GPR;
	std::size_t start = 0;
	std::size_t end = 0;
	PhysicalReg reg = PhysicalReg::Invalid;
	bool spilled = false;
	bool crossesCall = false;
	int32_t spillSlot = -1;
};

std::vector<PhysicalReg> regsFor(RegisterClass regClass)
{
	if (regClass == RegisterClass::FPR) {
		return std::vector<PhysicalReg>(std::begin(fprRegs), std::end(fprRegs));
	}
	return std::vector<PhysicalReg>(std::begin(gprRegs), std::end(gprRegs));
}

std::vector<PhysicalReg> callerSavedRegsFor(RegisterClass regClass)
{
	if (regClass == RegisterClass::FPR) {
		return std::vector<PhysicalReg>(std::begin(callerSavedFPRs), std::end(callerSavedFPRs));
	}
	return std::vector<PhysicalReg>(std::begin(callerSavedGPRs), std::end(callerSavedGPRs));
}

std::vector<PhysicalReg> scratchFor(RegisterClass regClass)
{
	if (regClass == RegisterClass::FPR) {
		return std::vector<PhysicalReg>(std::begin(fprScratch), std::end(fprScratch));
	}
	return std::vector<PhysicalReg>(std::begin(gprScratch), std::end(gprScratch));
}

bool isFPR(PhysicalReg reg)
{
	return reg >= PhysicalReg::FA0 && reg <= PhysicalReg::FS11;
}

bool containsReg(const std::vector<PhysicalReg> & regs, PhysicalReg reg)
{
	return std::find(regs.begin(), regs.end(), reg) != regs.end();
}

MachineOpcode spillLoadOpcode(RegisterClass regClass)
{
	return regClass == RegisterClass::FPR ? MachineOpcode::FLW : MachineOpcode::LD;
}

MachineOpcode spillStoreOpcode(RegisterClass regClass)
{
	return regClass == RegisterClass::FPR ? MachineOpcode::FSW : MachineOpcode::SD;
}

void touchInterval(
	std::unordered_map<int32_t, Interval> & intervals,
	const MachineFunction & function,
	int32_t vreg,
	std::size_t pos)
{
	if (vreg < 0) {
		return;
	}
	auto & interval = intervals[vreg];
	if (interval.vreg < 0) {
		interval.vreg = vreg;
		interval.regClass = function.registerClass(vreg);
		interval.start = pos;
		interval.end = pos;
	} else {
		interval.start = std::min(interval.start, pos);
		interval.end = std::max(interval.end, pos);
	}
}

std::vector<Interval> collectIntervals(const MachineFunction & function)
{
	std::unordered_map<int32_t, Interval> intervals;
	std::vector<std::size_t> callPositions;
	std::size_t pos = 0;
	for (MachineBlockIndex blockIndex = 0; blockIndex < function.blocks().size(); ++blockIndex) {
		const auto & block = function.blocks()[blockIndex];
		for (const auto & inst: block.instructions()) {
			if (inst.opcode == MachineOpcode::CALL) {
				callPositions.push_back(pos);
			}
			for (const auto & operand: inst.operands) {
				if (operand.kind == MachineOperandKind::VirtualReg) {
					touchInterval(intervals, function, operand.vreg, pos);
				} else if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
					touchInterval(intervals, function, operand.memoryBaseVReg, pos);
				}
			}
			++pos;
		}
	}

	MachineCFGBuilder cfgBuilder;
	auto functionCopy = function;
	cfgBuilder.run(functionCopy);
	MachineLivenessAnalysis livenessAnalysis;
	MachineLivenessResult liveness = livenessAnalysis.run(functionCopy);
	for (MachineBlockIndex blockIndex = 0; blockIndex < functionCopy.blocks().size(); ++blockIndex) {
		std::size_t blockStart = 0;
		for (MachineBlockIndex index = 0; index < blockIndex; ++index) {
			blockStart += functionCopy.blocks()[index].instructions().size();
		}
		const std::size_t blockEnd =
			blockStart + functionCopy.blocks()[blockIndex].instructions().size();
		for (int32_t vreg: liveness.block(blockIndex).liveIn) {
			touchInterval(intervals, function, vreg, blockStart);
		}
		for (int32_t vreg: liveness.block(blockIndex).liveOut) {
			touchInterval(intervals, function, vreg, blockEnd);
		}
	}

	std::vector<Interval> result;
	result.reserve(intervals.size());
	for (auto & entry: intervals) {
		for (std::size_t callPos: callPositions) {
			if (entry.second.start < callPos && callPos < entry.second.end) {
				entry.second.crossesCall = true;
				break;
			}
		}
		result.push_back(entry.second);
	}
	std::sort(result.begin(), result.end(), [](const Interval & lhs, const Interval & rhs) {
		if (lhs.start != rhs.start) {
			return lhs.start < rhs.start;
		}
		return lhs.end < rhs.end;
	});
	return result;
}

void expireOldIntervals(
	std::vector<Interval *> & active,
	std::vector<PhysicalReg> & freeCalleeRegs,
	std::vector<PhysicalReg> & freeCallerRegs,
	std::size_t start)
{
	for (auto iter = active.begin(); iter != active.end();) {
		if ((*iter)->end >= start) {
			++iter;
			continue;
		}
		if (containsReg(callerSavedRegsFor((*iter)->regClass), (*iter)->reg)) {
			freeCallerRegs.push_back((*iter)->reg);
		} else {
			freeCalleeRegs.push_back((*iter)->reg);
		}
		iter = active.erase(iter);
	}
}

void sortActive(std::vector<Interval *> & active)
{
	std::sort(active.begin(), active.end(), [](const Interval * lhs, const Interval * rhs) {
		return lhs->end < rhs->end;
	});
}

void allocateClass(std::vector<Interval *> intervals)
{
	if (intervals.empty()) {
		return;
	}

	std::vector<PhysicalReg> freeCalleeRegs = regsFor(intervals.front()->regClass);
	std::vector<PhysicalReg> freeCallerRegs = callerSavedRegsFor(intervals.front()->regClass);
	std::vector<Interval *> active;
	for (Interval * interval: intervals) {
		expireOldIntervals(active, freeCalleeRegs, freeCallerRegs, interval->start);
		sortActive(active);
		if (!interval->crossesCall && !freeCallerRegs.empty()) {
			interval->reg = freeCallerRegs.back();
			freeCallerRegs.pop_back();
			active.push_back(interval);
			continue;
		}
		if (!freeCalleeRegs.empty()) {
			interval->reg = freeCalleeRegs.back();
			freeCalleeRegs.pop_back();
			active.push_back(interval);
			continue;
		}

		auto spill = active.end();
		for (auto iter = active.begin(); iter != active.end(); ++iter) {
			if (interval->crossesCall && containsReg(callerSavedRegsFor(interval->regClass), (*iter)->reg)) {
				continue;
			}
			if (spill == active.end() || (*spill)->end < (*iter)->end) {
				spill = iter;
			}
		}
		if (spill != active.end() && (*spill)->end > interval->end) {
			interval->reg = (*spill)->reg;
			(*spill)->reg = PhysicalReg::Invalid;
			(*spill)->spilled = true;
			*spill = interval;
		} else {
			interval->spilled = true;
		}
	}
}

void allocateIntervals(std::vector<Interval> & intervals)
{
	std::vector<Interval *> gprs;
	std::vector<Interval *> fprs;
	for (auto & interval: intervals) {
		if (interval.regClass == RegisterClass::FPR) {
			fprs.push_back(&interval);
		} else {
			gprs.push_back(&interval);
		}
	}
	allocateClass(gprs);
	allocateClass(fprs);
}

std::unordered_map<int32_t, Interval *> indexIntervals(std::vector<Interval> & intervals)
{
	std::unordered_map<int32_t, Interval *> result;
	for (auto & interval: intervals) {
		result[interval.vreg] = &interval;
	}
	return result;
}

MachineOperand assignedOperand(const MachineOperand & operand, const std::unordered_map<int32_t, Interval *> & intervals)
{
	MachineOperand rewritten = operand;
	if (operand.kind == MachineOperandKind::VirtualReg) {
		auto iter = intervals.find(operand.vreg);
		if (iter != intervals.end() && !iter->second->spilled) {
			rewritten.kind = MachineOperandKind::PhysicalReg;
			rewritten.preg = iter->second->reg;
			rewritten.vreg = -1;
		}
		return rewritten;
	}
	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		auto iter = intervals.find(operand.memoryBaseVReg);
		if (iter != intervals.end() && !iter->second->spilled) {
			rewritten.memoryBaseIsPhysical = true;
			rewritten.memoryBasePreg = iter->second->reg;
			rewritten.memoryBaseVReg = -1;
		}
	}
	return rewritten;
}

PhysicalReg nextScratch(RegisterClass regClass, std::size_t & gprIndex, std::size_t & fprIndex)
{
	auto scratch = scratchFor(regClass);
	std::size_t & index = regClass == RegisterClass::FPR ? fprIndex : gprIndex;
	if (index >= scratch.size()) {
		return scratch.back();
	}
	return scratch[index++];
}

void rewriteInstruction(
	const MachineInstr & inst,
	const std::unordered_map<int32_t, Interval *> & intervals,
	std::vector<MachineInstr> & output)
{
	MachineInstr rewritten = inst;
	std::vector<MachineInstr> before;
	std::vector<MachineInstr> after;
	std::size_t gprScratchIndex = 0;
	std::size_t fprScratchIndex = 0;

	for (auto & operand: rewritten.operands) {
		if (operand.kind == MachineOperandKind::VirtualReg) {
			auto iter = intervals.find(operand.vreg);
			if (iter == intervals.end()) {
				continue;
			}
			Interval * interval = iter->second;
			if (!interval->spilled) {
				operand = assignedOperand(operand, intervals);
				continue;
			}

			PhysicalReg scratch = nextScratch(interval->regClass, gprScratchIndex, fprScratchIndex);
			if (operand.role == MachineOperandRole::Use) {
				before.push_back(MachineInstr::make(
					spillLoadOpcode(interval->regClass),
					{MachineOperand::pregDef(scratch), MachineOperand::spillSlotOperand(interval->spillSlot)}));
				operand = MachineOperand::pregUse(scratch);
			} else if (operand.role == MachineOperandRole::Def) {
				operand = MachineOperand::pregDef(scratch);
				after.push_back(MachineInstr::make(
					spillStoreOpcode(interval->regClass),
					{MachineOperand::pregUse(scratch), MachineOperand::spillSlotOperand(interval->spillSlot)}));
			}
			continue;
		}

		if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
			auto iter = intervals.find(operand.memoryBaseVReg);
			if (iter == intervals.end()) {
				continue;
			}
			Interval * interval = iter->second;
			if (!interval->spilled) {
				operand = assignedOperand(operand, intervals);
				continue;
			}

			PhysicalReg scratch = nextScratch(RegisterClass::GPR, gprScratchIndex, fprScratchIndex);
			before.push_back(MachineInstr::make(
				MachineOpcode::LD,
				{MachineOperand::pregDef(scratch), MachineOperand::spillSlotOperand(interval->spillSlot)}));
			operand.memoryBaseIsPhysical = true;
			operand.memoryBasePreg = scratch;
			operand.memoryBaseVReg = -1;
		}
	}

	output.insert(output.end(), before.begin(), before.end());
	output.push_back(std::move(rewritten));
	output.insert(output.end(), after.begin(), after.end());
}

void assignSpillSlots(std::vector<Interval> & intervals, FunctionFrameLayout & layout)
{
	for (auto & interval: intervals) {
		if (!interval.spilled) {
			continue;
		}
		interval.spillSlot = layout.createSpillSlot(interval.regClass == RegisterClass::FPR ? 4 : 8, 8);
	}
}

std::vector<PhysicalReg> usedCalleeSavedRegs(const std::vector<Interval> & intervals)
{
	std::set<PhysicalReg> used;
	for (const auto & interval: intervals) {
		if (!interval.spilled && interval.reg != PhysicalReg::Invalid) {
			used.insert(interval.reg);
		}
	}

	std::vector<PhysicalReg> ordered;
	for (PhysicalReg reg: gprRegs) {
		if (used.find(reg) != used.end()) {
			ordered.push_back(reg);
		}
	}
	for (PhysicalReg reg: fprRegs) {
		if (used.find(reg) != used.end()) {
			ordered.push_back(reg);
		}
	}
	return ordered;
}

bool isFramePointerSetup(const MachineInstr & inst)
{
	return inst.opcode == MachineOpcode::ADDI && inst.operands.size() >= 3 &&
		   inst.operands[0].kind == MachineOperandKind::PhysicalReg &&
		   inst.operands[1].kind == MachineOperandKind::PhysicalReg && inst.operands[0].preg == PhysicalReg::FP &&
		   inst.operands[1].preg == PhysicalReg::SP;
}

bool isSavedReturnAddressLoad(const MachineInstr & inst)
{
	return inst.opcode == MachineOpcode::LD && inst.operands.size() >= 2 &&
		   inst.operands[0].kind == MachineOperandKind::PhysicalReg && inst.operands[0].preg == PhysicalReg::RA &&
		   inst.operands[1].kind == MachineOperandKind::Memory && inst.operands[1].memoryBaseIsPhysical &&
		   ((inst.operands[1].memoryBasePreg == PhysicalReg::FP &&
			 inst.operands[1].memoryOffset == FunctionFrameLayout::savedRaOffset) ||
		    inst.operands[1].memoryBasePreg == PhysicalReg::SP);
}

bool isOldFramePointerLoad(const MachineInstr & inst)
{
	return inst.opcode == MachineOpcode::LD && inst.operands.size() >= 2 &&
		   inst.operands[1].kind == MachineOperandKind::Memory && inst.operands[1].memoryBaseIsPhysical &&
		   inst.operands[1].memoryBasePreg == PhysicalReg::FP &&
		   inst.operands[1].memoryOffset == FunctionFrameLayout::savedFpOffset;
}

bool isEpilogueRestorePoint(const MachineInstr & inst)
{
	return isSavedReturnAddressLoad(inst) || isOldFramePointerLoad(inst);
}

void rewriteFrameSetup(MachineFunction & function, const FunctionFrameLayout & layout)
{
	if (function.blocks().empty()) {
		return;
	}

	const int32_t frameSize = layout.frameSize();
	for (auto & inst: function.blocks()[0].instructions()) {
		if (!MachineLegalizer::isFrameSetupInstruction(inst)) {
			continue;
		}
		if (inst.opcode == MachineOpcode::ADDI && inst.operands.size() >= 3 &&
			inst.operands[0].kind == MachineOperandKind::PhysicalReg &&
			inst.operands[1].kind == MachineOperandKind::PhysicalReg &&
			inst.operands[2].kind == MachineOperandKind::Immediate) {
			if (inst.operands[0].preg == PhysicalReg::SP && inst.operands[1].preg == PhysicalReg::SP) {
				inst.operands[2].imm = -frameSize;
			} else if (inst.operands[0].preg == PhysicalReg::FP && inst.operands[1].preg == PhysicalReg::SP) {
				inst.operands[2].imm = frameSize;
			}
			continue;
		}
		if (inst.opcode == MachineOpcode::SD && inst.operands.size() >= 2 &&
			inst.operands[0].kind == MachineOperandKind::PhysicalReg &&
			inst.operands[1].kind == MachineOperandKind::Memory && inst.operands[1].memoryBaseIsPhysical &&
			inst.operands[1].memoryBasePreg == PhysicalReg::SP) {
			if (inst.operands[0].preg == PhysicalReg::RA) {
				inst.operands[1].memoryOffset = frameSize + FunctionFrameLayout::savedRaOffset;
			} else if (inst.operands[0].preg == PhysicalReg::FP) {
				inst.operands[1].memoryOffset = frameSize + FunctionFrameLayout::savedFpOffset;
			}
		}
	}

	for (auto & block: function.blocks()) {
		for (auto & inst: block.instructions()) {
			if (inst.opcode == MachineOpcode::LD && inst.operands.size() >= 2 &&
				inst.operands[0].kind == MachineOperandKind::PhysicalReg &&
				inst.operands[1].kind == MachineOperandKind::Memory && inst.operands[1].memoryBaseIsPhysical &&
				inst.operands[1].memoryBasePreg == PhysicalReg::SP) {
				if (inst.operands[0].preg == PhysicalReg::RA) {
					inst.operands[1].memoryOffset = frameSize + FunctionFrameLayout::savedRaOffset;
				} else if (inst.operands[0].preg == PhysicalReg::FP) {
					inst.operands[1].memoryOffset = frameSize + FunctionFrameLayout::savedFpOffset;
				}
				continue;
			}
			if (inst.opcode == MachineOpcode::ADDI && inst.operands.size() >= 3 &&
				inst.operands[0].kind == MachineOperandKind::PhysicalReg &&
				inst.operands[1].kind == MachineOperandKind::PhysicalReg &&
				inst.operands[2].kind == MachineOperandKind::Immediate && inst.operands[0].preg == PhysicalReg::SP &&
				inst.operands[1].preg == PhysicalReg::SP && inst.operands[2].imm > 0) {
				inst.operands[2].imm = frameSize;
			}
		}
	}
}

std::map<PhysicalReg, int32_t> createCalleeSavedSlots(
	FunctionFrameLayout & layout,
	const std::vector<PhysicalReg> & regs)
{
	std::map<PhysicalReg, int32_t> slots;
	for (PhysicalReg reg: regs) {
		slots.emplace(reg, layout.createSpillSlot(isFPR(reg) ? 4 : 8, 8));
	}
	return slots;
}

void insertCalleeSavedPrologue(MachineFunction & function, const std::map<PhysicalReg, int32_t> & slots)
{
	if (slots.empty() || function.blocks().empty()) {
		return;
	}
	auto & instructions = function.blocks()[0].instructions();
	auto insertPos = instructions.begin();
	for (auto iter = instructions.begin(); iter != instructions.end(); ++iter) {
		if (isFramePointerSetup(*iter)) {
			insertPos = std::next(iter);
			break;
		}
	}

	std::vector<MachineInstr> saves;
	for (const auto & entry: slots) {
		saves.push_back(MachineInstr::make(
			isFPR(entry.first) ? MachineOpcode::FSW : MachineOpcode::SD,
			{MachineOperand::pregUse(entry.first), MachineOperand::spillSlotOperand(entry.second)}));
	}
	instructions.insert(insertPos, saves.begin(), saves.end());
}

void insertCalleeSavedEpilogues(MachineFunction & function, const std::map<PhysicalReg, int32_t> & slots)
{
	if (slots.empty()) {
		return;
	}
	std::vector<MachineInstr> restores;
	for (auto iter = slots.rbegin(); iter != slots.rend(); ++iter) {
		restores.push_back(MachineInstr::make(
			isFPR(iter->first) ? MachineOpcode::FLW : MachineOpcode::LD,
			{MachineOperand::pregDef(iter->first), MachineOperand::spillSlotOperand(iter->second)}));
	}
	for (auto & block: function.blocks()) {
		auto & instructions = block.instructions();
		for (auto iter = instructions.begin(); iter != instructions.end(); ++iter) {
			if (isEpilogueRestorePoint(*iter)) {
				instructions.insert(iter, restores.begin(), restores.end());
				break;
			}
		}
	}
}

bool verifyNoVirtualRegs(const MachineFunction & function)
{
	for (const auto & block: function.blocks()) {
		for (const auto & inst: block.instructions()) {
			for (const auto & operand: inst.operands) {
				if (operand.kind == MachineOperandKind::VirtualReg) {
					return false;
				}
				if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
					return false;
				}
			}
		}
	}
	return true;
}

} // namespace

bool LinearScanRegisterAllocator::run(MachineFunction & function, FunctionFrameLayout & layout) const
{
	MachineLegalizer legalizer(layout);
	legalizer.run(function, true);

	auto intervals = collectIntervals(function);
	allocateIntervals(intervals);
	assignSpillSlots(intervals, layout);

	const auto savedRegs = usedCalleeSavedRegs(intervals);
	const auto savedSlots = createCalleeSavedSlots(layout, savedRegs);
	insertCalleeSavedPrologue(function, savedSlots);
	insertCalleeSavedEpilogues(function, savedSlots);
	rewriteFrameSetup(function, layout);

	auto intervalIndex = indexIntervals(intervals);
	for (auto & block: function.blocks()) {
		std::vector<MachineInstr> rewritten;
		rewritten.reserve(block.instructions().size());
		for (const auto & inst: block.instructions()) {
			rewriteInstruction(inst, intervalIndex, rewritten);
		}
		block.instructions() = std::move(rewritten);
	}

	legalizer.run(function, false);
	return verifyNoVirtualRegs(function);
}
