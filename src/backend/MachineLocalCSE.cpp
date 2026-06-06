#include "MachineLocalCSE.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

bool isPureOpcode(MachineOpcode opcode)
{
	switch (opcode) {
		case MachineOpcode::LI:
		case MachineOpcode::LA:
		case MachineOpcode::ADDI:
		case MachineOpcode::ADDIW:
		case MachineOpcode::ADD:
		case MachineOpcode::ADDW:
		case MachineOpcode::SUBW:
		case MachineOpcode::MUL:
		case MachineOpcode::MULW:
		case MachineOpcode::SLLI:
		case MachineOpcode::AND:
		case MachineOpcode::OR:
		case MachineOpcode::XOR:
		case MachineOpcode::ANDI:
		case MachineOpcode::XORI:
		case MachineOpcode::SLT:
		case MachineOpcode::SEQZ:
		case MachineOpcode::SNEZ:
			return true;
		default:
			return false;
	}
}

bool isCommutativeOpcode(MachineOpcode opcode)
{
	return opcode == MachineOpcode::ADD || opcode == MachineOpcode::ADDW || opcode == MachineOpcode::MUL ||
		   opcode == MachineOpcode::MULW || opcode == MachineOpcode::AND || opcode == MachineOpcode::OR ||
		   opcode == MachineOpcode::XOR;
}

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

bool rewriteOperand(MachineOperand & operand, const std::unordered_map<int32_t, MachineOperand> & aliases)
{
	if (operand.kind == MachineOperandKind::VirtualReg && operand.role == MachineOperandRole::Use) {
		auto iter = aliases.find(operand.vreg);
		if (iter != aliases.end()) {
			if (iter->second.kind == MachineOperandKind::VirtualReg && iter->second.vreg == operand.vreg) {
				return false;
			}
			operand = iter->second.asUse();
			return true;
		}
	}

	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		auto iter = aliases.find(operand.memoryBaseVReg);
		if (iter != aliases.end() && iter->second.kind == MachineOperandKind::VirtualReg) {
			operand.memoryBaseVReg = iter->second.vreg;
			return true;
		}
	}

	return false;
}

std::string operandKey(const MachineOperand & operand)
{
	switch (operand.kind) {
		case MachineOperandKind::VirtualReg:
			return "v" + std::to_string(operand.vreg) + ":" + std::to_string(static_cast<int>(operand.regClass));
		case MachineOperandKind::PhysicalReg:
			if (operand.preg == PhysicalReg::Zero) {
				return "zero";
			}
			return "";
		case MachineOperandKind::Immediate:
			return "i" + std::to_string(operand.imm);
		case MachineOperandKind::GlobalSymbol:
			return "g" + operand.text;
		default:
			return "";
	}
}

bool isAllowedPureOperand(const MachineOperand & operand)
{
	if (operand.kind == MachineOperandKind::VirtualReg && operand.role == MachineOperandRole::Use) {
		return true;
	}
	if (operand.kind == MachineOperandKind::PhysicalReg && operand.role == MachineOperandRole::Use &&
		operand.preg == PhysicalReg::Zero) {
		return true;
	}
	return operand.kind == MachineOperandKind::Immediate || operand.kind == MachineOperandKind::GlobalSymbol;
}

bool pureExpressionKey(const MachineInstr & inst, std::string & key, int32_t & defVReg)
{
	defVReg = -1;
	key.clear();

	if (!isPureOpcode(inst.opcode) || hasPhysicalDef(inst) || inst.operands.empty()) {
		return false;
	}

	const auto & dst = inst.operands[0];
	if (dst.kind != MachineOperandKind::VirtualReg || dst.role != MachineOperandRole::Def) {
		return false;
	}
	defVReg = dst.vreg;

	std::vector<std::string> operands;
	operands.reserve(inst.operands.size() - 1);
	for (std::size_t index = 1; index < inst.operands.size(); ++index) {
		const auto & operand = inst.operands[index];
		if (!isAllowedPureOperand(operand)) {
			return false;
		}
		if (operand.kind == MachineOperandKind::VirtualReg && operand.vreg == defVReg) {
			return false;
		}
		std::string part = operandKey(operand);
		if (part.empty()) {
			return false;
		}
		operands.push_back(std::move(part));
	}

	if (isCommutativeOpcode(inst.opcode)) {
		std::sort(operands.begin(), operands.end());
	}

	key = std::to_string(static_cast<int>(inst.opcode)) + ":" +
		  std::to_string(static_cast<int>(dst.regClass));
	for (const auto & operand: operands) {
		key += ":" + operand;
	}
	return true;
}

void eraseExpressionsMentioning(
	std::unordered_map<std::string, MachineOperand> & expressions,
	int32_t vreg)
{
	if (vreg < 0) {
		return;
	}

	for (auto iter = expressions.begin(); iter != expressions.end();) {
		if (iter->second.kind == MachineOperandKind::VirtualReg && iter->second.vreg == vreg) {
			iter = expressions.erase(iter);
		} else {
			++iter;
		}
	}
}

} // namespace

bool MachineLocalCSE::run(MachineFunction & function) const
{
	bool changed = false;

	for (auto & block: function.blocks()) {
		std::unordered_map<int32_t, MachineOperand> aliases;
		std::unordered_map<std::string, MachineOperand> expressions;
		std::unordered_set<int32_t> definedVRegs;
		std::vector<MachineInstr> rewritten;
		rewritten.reserve(block.instructions().size());

		for (auto inst: block.instructions()) {
			for (auto & operand: inst.operands) {
				changed = rewriteOperand(operand, aliases) || changed;
			}

			if (inst.opcode == MachineOpcode::CALL) {
				expressions.clear();
			}

			std::string key;
			int32_t defVReg = -1;
			if (pureExpressionKey(inst, key, defVReg)) {
				if (definedVRegs.find(defVReg) != definedVRegs.end()) {
					aliases.erase(defVReg);
					expressions.clear();
				}

				auto iter = expressions.find(key);
				if (iter != expressions.end()) {
					aliases[defVReg] = iter->second.asUse();
					definedVRegs.insert(defVReg);
					changed = true;
					continue;
				}

				MachineOperand result = inst.operands[0].asUse();
				expressions.emplace(std::move(key), result);
				definedVRegs.insert(defVReg);
				rewritten.push_back(std::move(inst));
				continue;
			}

			for (const auto & operand: inst.operands) {
				if (operand.kind == MachineOperandKind::VirtualReg && operand.role == MachineOperandRole::Def) {
					if (definedVRegs.find(operand.vreg) != definedVRegs.end()) {
						expressions.clear();
					}
					definedVRegs.insert(operand.vreg);
					aliases.erase(operand.vreg);
					eraseExpressionsMentioning(expressions, operand.vreg);
				}
			}

			rewritten.push_back(std::move(inst));
		}

		if (rewritten.size() != block.instructions().size()) {
			changed = true;
		}
		block.instructions() = std::move(rewritten);
	}

	return changed;
}
