#include "MachineInstCombine.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace {

struct ImmediateDef {
	int64_t value = 0;
};

struct AddiDef {
	MachineOperand base;
	int64_t offset = 0;
	std::size_t foldableUses = 0;
	bool hasBadMemoryUse = false;
};

bool isSigned12Bit(int64_t value)
{
	return value >= -2048 && value <= 2047;
}

bool isMemoryOpcode(MachineOpcode opcode)
{
	return opcode == MachineOpcode::LW || opcode == MachineOpcode::LD || opcode == MachineOpcode::FLW ||
		   opcode == MachineOpcode::SW || opcode == MachineOpcode::SD || opcode == MachineOpcode::FSW;
}

bool isVirtualDef(const MachineOperand & operand)
{
	return operand.kind == MachineOperandKind::VirtualReg && operand.role == MachineOperandRole::Def;
}

bool isVirtualUse(const MachineOperand & operand)
{
	return operand.kind == MachineOperandKind::VirtualReg && operand.role == MachineOperandRole::Use;
}

bool isRegisterUse(const MachineOperand & operand)
{
	return (operand.kind == MachineOperandKind::VirtualReg || operand.kind == MachineOperandKind::PhysicalReg) &&
		   operand.role == MachineOperandRole::Use;
}

void recordImmediateDef(
	std::unordered_map<int32_t, ImmediateDef> & immediates,
	std::unordered_set<int32_t> & duplicateDefs,
	const MachineInstr & inst)
{
	if (inst.opcode != MachineOpcode::LI || inst.operands.size() < 2 || !isVirtualDef(inst.operands[0]) ||
		inst.operands[1].kind != MachineOperandKind::Immediate) {
		return;
	}

	const int32_t vreg = inst.operands[0].vreg;
	if (duplicateDefs.find(vreg) != duplicateDefs.end()) {
		return;
	}
	if (immediates.find(vreg) != immediates.end()) {
		immediates.erase(vreg);
		duplicateDefs.insert(vreg);
		return;
	}

	immediates.emplace(vreg, ImmediateDef{inst.operands[1].imm});
}

void collectImmediateDefs(
	const MachineFunction & function,
	std::unordered_map<int32_t, ImmediateDef> & immediates)
{
	std::unordered_set<int32_t> duplicateDefs;
	for (const auto & block: function.blocks()) {
		for (const auto & inst: block.instructions()) {
			recordImmediateDef(immediates, duplicateDefs, inst);
		}
	}
}

bool matchSigned12Immediate(
	const MachineOperand & operand,
	const std::unordered_map<int32_t, ImmediateDef> & immediates,
	int64_t & value)
{
	if (!isVirtualUse(operand)) {
		return false;
	}

	auto iter = immediates.find(operand.vreg);
	if (iter == immediates.end() || !isSigned12Bit(iter->second.value)) {
		return false;
	}

	value = iter->second.value;
	return true;
}

bool rewriteCommutativeImmediate(
	MachineInstr & inst,
	MachineOpcode replacement,
	const std::unordered_map<int32_t, ImmediateDef> & immediates)
{
	if (inst.operands.size() < 3) {
		return false;
	}

	int64_t lhsImm = 0;
	int64_t rhsImm = 0;
	const bool lhsIsImm = matchSigned12Immediate(inst.operands[1], immediates, lhsImm);
	const bool rhsIsImm = matchSigned12Immediate(inst.operands[2], immediates, rhsImm);
	if (lhsIsImm == rhsIsImm) {
		return false;
	}

	MachineOperand dst = inst.operands[0];
	MachineOperand base = lhsIsImm ? inst.operands[2] : inst.operands[1];
	if (!isRegisterUse(base)) {
		return false;
	}

	inst.opcode = replacement;
	inst.operands = {dst, base.asUse(), MachineOperand::immValue(lhsIsImm ? lhsImm : rhsImm)};
	return true;
}

bool combineImmediateOps(MachineFunction & function)
{
	std::unordered_map<int32_t, ImmediateDef> immediates;
	collectImmediateDefs(function, immediates);
	if (immediates.empty()) {
		return false;
	}

	bool changed = false;
	for (auto & block: function.blocks()) {
		for (auto & inst: block.instructions()) {
			switch (inst.opcode) {
				case MachineOpcode::ADD:
					changed = rewriteCommutativeImmediate(inst, MachineOpcode::ADDI, immediates) || changed;
					break;
				case MachineOpcode::ADDW:
					changed = rewriteCommutativeImmediate(inst, MachineOpcode::ADDIW, immediates) || changed;
					break;
				case MachineOpcode::AND:
					changed = rewriteCommutativeImmediate(inst, MachineOpcode::ANDI, immediates) || changed;
					break;
				default:
					break;
			}
		}
	}

	return changed;
}

void countUse(const MachineOperand & operand, std::unordered_map<int32_t, std::size_t> & useCounts)
{
	if (isVirtualUse(operand)) {
		++useCounts[operand.vreg];
		return;
	}
	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		++useCounts[operand.memoryBaseVReg];
	}
}

void collectUseCounts(const MachineFunction & function, std::unordered_map<int32_t, std::size_t> & useCounts)
{
	for (const auto & block: function.blocks()) {
		for (const auto & inst: block.instructions()) {
			for (const auto & operand: inst.operands) {
				countUse(operand, useCounts);
			}
		}
	}
}

void recordAddiDef(
	std::unordered_map<int32_t, AddiDef> & addiDefs,
	std::unordered_set<int32_t> & duplicateDefs,
	const MachineInstr & inst)
{
	if (inst.opcode != MachineOpcode::ADDI || inst.operands.size() < 3 || !isVirtualDef(inst.operands[0]) ||
		!isVirtualUse(inst.operands[1]) || inst.operands[2].kind != MachineOperandKind::Immediate) {
		return;
	}

	const int32_t vreg = inst.operands[0].vreg;
	if (duplicateDefs.find(vreg) != duplicateDefs.end()) {
		return;
	}
	if (addiDefs.find(vreg) != addiDefs.end()) {
		addiDefs.erase(vreg);
		duplicateDefs.insert(vreg);
		return;
	}

	addiDefs.emplace(vreg, AddiDef{inst.operands[1].asUse(), inst.operands[2].imm, 0, false});
}

void collectAddiDefs(const MachineFunction & function, std::unordered_map<int32_t, AddiDef> & addiDefs)
{
	std::unordered_set<int32_t> duplicateDefs;
	for (const auto & block: function.blocks()) {
		for (const auto & inst: block.instructions()) {
			recordAddiDef(addiDefs, duplicateDefs, inst);
		}
	}
}

void collectFoldableMemoryUses(MachineFunction & function, std::unordered_map<int32_t, AddiDef> & addiDefs)
{
	for (const auto & block: function.blocks()) {
		for (const auto & inst: block.instructions()) {
			if (!isMemoryOpcode(inst.opcode)) {
				continue;
			}
			for (const auto & operand: inst.operands) {
				if (operand.kind != MachineOperandKind::Memory || operand.memoryBaseIsPhysical) {
					continue;
				}
				auto iter = addiDefs.find(operand.memoryBaseVReg);
				if (iter == addiDefs.end()) {
					continue;
				}
				if (isSigned12Bit(operand.memoryOffset + iter->second.offset)) {
					++iter->second.foldableUses;
				} else {
					iter->second.hasBadMemoryUse = true;
				}
			}
		}
	}
}

bool isProfitableAddiFold(
	int32_t vreg,
	const AddiDef & addiDef,
	const std::unordered_map<int32_t, std::size_t> & useCounts)
{
	auto iter = useCounts.find(vreg);
	if (iter == useCounts.end() || iter->second == 0 || addiDef.hasBadMemoryUse) {
		return false;
	}
	return iter->second == addiDef.foldableUses;
}

bool foldAddiMemoryOperands(MachineFunction & function)
{
	std::unordered_map<int32_t, AddiDef> addiDefs;
	std::unordered_map<int32_t, std::size_t> useCounts;
	collectAddiDefs(function, addiDefs);
	if (addiDefs.empty()) {
		return false;
	}

	collectUseCounts(function, useCounts);
	collectFoldableMemoryUses(function, addiDefs);

	bool changed = false;
	for (auto & block: function.blocks()) {
		for (auto & inst: block.instructions()) {
			if (!isMemoryOpcode(inst.opcode)) {
				continue;
			}
			for (auto & operand: inst.operands) {
				if (operand.kind != MachineOperandKind::Memory || operand.memoryBaseIsPhysical) {
					continue;
				}
				auto iter = addiDefs.find(operand.memoryBaseVReg);
				if (iter == addiDefs.end() || !isProfitableAddiFold(operand.memoryBaseVReg, iter->second, useCounts)) {
					continue;
				}
				operand.memoryBaseVReg = iter->second.base.vreg;
				operand.memoryOffset += iter->second.offset;
				changed = true;
			}
		}
	}

	return changed;
}

} // namespace

bool MachineInstCombine::run(MachineFunction & function) const
{
	bool changed = false;
	changed = combineImmediateOps(function) || changed;
	changed = foldAddiMemoryOperands(function) || changed;
	return changed;
}
