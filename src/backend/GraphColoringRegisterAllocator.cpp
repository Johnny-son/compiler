#include "backend/include/GraphColoringRegisterAllocator.h"

#include "backend/include/MachineCFG.h"
#include "backend/include/MachineLegalizer.h"
#include "backend/include/MachineLiveness.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

constexpr int maxIterations = 128;

const PhysicalReg allocatableGPRs[] = {
	PhysicalReg::T0,
	PhysicalReg::T1,
	PhysicalReg::T2,
	PhysicalReg::T3,
	PhysicalReg::T4,
	PhysicalReg::T5,
	PhysicalReg::T6,
};

const PhysicalReg allocatableFPRs[] = {
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

std::vector<PhysicalReg> allocatableRegsFor(RegisterClass regClass)
{
	if (regClass == RegisterClass::FPR) {
		return std::vector<PhysicalReg>(std::begin(allocatableFPRs), std::end(allocatableFPRs));
	}
	return std::vector<PhysicalReg>(std::begin(allocatableGPRs), std::end(allocatableGPRs));
}

MachineOpcode spillLoadOpcode(RegisterClass regClass)
{
	return regClass == RegisterClass::FPR ? MachineOpcode::FLW : MachineOpcode::LD;
}

MachineOpcode spillStoreOpcode(RegisterClass regClass)
{
	return regClass == RegisterClass::FPR ? MachineOpcode::FSW : MachineOpcode::SD;
}

struct InterferenceGraph {
	std::map<int32_t, std::set<int32_t>> neighbors;
	std::map<int32_t, RegisterClass> classes;
	std::set<int32_t> mustSpill;

	void addNode(int32_t vreg, RegisterClass regClass = RegisterClass::GPR)
	{
		if (vreg >= 0) {
			neighbors.try_emplace(vreg);
			auto iter = classes.find(vreg);
			if (iter == classes.end()) {
				classes.emplace(vreg, regClass);
			}
		}
	}

	void addEdge(int32_t lhs, int32_t rhs)
	{
		if (lhs < 0 || rhs < 0 || lhs == rhs) {
			return;
		}
		addNode(lhs);
		addNode(rhs);
		if (classes[lhs] != classes[rhs]) {
			return;
		}
		neighbors[lhs].insert(rhs);
		neighbors[rhs].insert(lhs);
	}

	int degree(int32_t vreg) const
	{
		auto iter = neighbors.find(vreg);
		return iter == neighbors.end() ? 0 : static_cast<int>(iter->second.size());
	}
};

struct ColoringResult {
	bool success = false;
	int32_t spillCandidate = -1;
	std::unordered_map<int32_t, PhysicalReg> assignment;
};

RegisterClass classOf(const MachineFunction & function, int32_t vreg)
{
	return function.registerClass(vreg);
}

void collectOperandNodes(const MachineFunction & function, const MachineOperand & operand, InterferenceGraph & graph)
{
	if (operand.kind == MachineOperandKind::VirtualReg) {
		graph.addNode(operand.vreg, classOf(function, operand.vreg));
		return;
	}

	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		graph.addNode(operand.memoryBaseVReg, classOf(function, operand.memoryBaseVReg));
	}
}

InterferenceGraph buildInterferenceGraph(const MachineFunction & function, const MachineLivenessResult & liveness)
{
	InterferenceGraph graph;

	for (MachineBlockIndex blockIndex = 0; blockIndex < function.blocks().size(); ++blockIndex) {
		const auto & block = function.blocks()[blockIndex];
		for (std::size_t instIndex = 0; instIndex < block.instructions().size(); ++instIndex) {
			const auto & inst = block.instructions()[instIndex];
			const auto & info = liveness.instruction(blockIndex, instIndex);

			for (const auto & operand: inst.operands) {
				collectOperandNodes(function, operand, graph);
			}
			for (int32_t vreg: info.liveIn) {
				graph.addNode(vreg, classOf(function, vreg));
			}
			for (int32_t vreg: info.liveOut) {
				graph.addNode(vreg, classOf(function, vreg));
			}

			for (int32_t def: info.def) {
				graph.addNode(def, classOf(function, def));
				for (int32_t live: info.liveOut) {
					graph.addEdge(def, live);
				}
			}

			if (inst.opcode == MachineOpcode::CALL) {
				graph.mustSpill.insert(info.liveOut.begin(), info.liveOut.end());
			}
		}
	}

	return graph;
}

std::vector<int32_t> nodesByDegree(const InterferenceGraph & graph)
{
	std::vector<int32_t> nodes;
	nodes.reserve(graph.neighbors.size());
	for (const auto & entry: graph.neighbors) {
		nodes.push_back(entry.first);
	}

	std::sort(nodes.begin(), nodes.end(), [&graph](int32_t lhs, int32_t rhs) {
		const int lhsDegree = graph.degree(lhs);
		const int rhsDegree = graph.degree(rhs);
		if (lhsDegree != rhsDegree) {
			return lhsDegree > rhsDegree;
		}
		return lhs < rhs;
	});
	return nodes;
}

int32_t highestDegreeNode(const std::set<int32_t> & nodes, const InterferenceGraph & graph)
{
	int32_t best = -1;
	int bestDegree = -1;
	for (int32_t node: nodes) {
		const int degree = graph.degree(node);
		if (degree > bestDegree || (degree == bestDegree && (best < 0 || node < best))) {
			best = node;
			bestDegree = degree;
		}
	}
	return best;
}

ColoringResult colorGraph(const InterferenceGraph & graph)
{
	ColoringResult result;

	for (int32_t node: nodesByDegree(graph)) {
		std::set<PhysicalReg> used;
		auto neighborIter = graph.neighbors.find(node);
		if (neighborIter != graph.neighbors.end()) {
			for (int32_t neighbor: neighborIter->second) {
				auto assigned = result.assignment.find(neighbor);
				if (assigned != result.assignment.end()) {
					used.insert(assigned->second);
				}
			}
		}

		std::optional<PhysicalReg> selected;
		auto classIter = graph.classes.find(node);
		const RegisterClass regClass = classIter != graph.classes.end() ? classIter->second : RegisterClass::GPR;
		for (PhysicalReg reg: allocatableRegsFor(regClass)) {
			if (used.find(reg) == used.end()) {
				selected = reg;
				break;
			}
		}

		if (!selected.has_value()) {
			result.success = false;
			result.spillCandidate = node;
			return result;
		}

		result.assignment[node] = *selected;
	}

	result.success = true;
	return result;
}

bool operandUsesVReg(const MachineOperand & operand, int32_t vreg)
{
	if (operand.kind == MachineOperandKind::VirtualReg) {
		return operand.role == MachineOperandRole::Use && operand.vreg == vreg;
	}
	return operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical && operand.memoryBaseVReg == vreg;
}

bool operandDefsVReg(const MachineOperand & operand, int32_t vreg)
{
	return operand.kind == MachineOperandKind::VirtualReg && operand.role == MachineOperandRole::Def && operand.vreg == vreg;
}

void replaceOperandUse(MachineOperand & operand, int32_t oldVReg, const MachineOperand & replacement)
{
	if (operand.kind == MachineOperandKind::VirtualReg && operand.role == MachineOperandRole::Use &&
		operand.vreg == oldVReg) {
		operand = replacement.asUse();
		return;
	}

	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical &&
		operand.memoryBaseVReg == oldVReg) {
		operand.memoryBaseVReg = replacement.vreg;
	}
}

void replaceOperandDef(MachineOperand & operand, int32_t oldVReg, const MachineOperand & replacement)
{
	if (operand.kind == MachineOperandKind::VirtualReg && operand.role == MachineOperandRole::Def &&
		operand.vreg == oldVReg) {
		operand = replacement.asDef();
	}
}

void rewriteSpill(MachineFunction & function, FunctionFrameLayout & layout, int32_t spilledVReg)
{
	const int32_t slot = layout.createSpillSlot();
	const RegisterClass regClass = function.registerClass(spilledVReg);

	for (auto & block: function.blocks()) {
		std::vector<MachineInstr> rewritten;
		rewritten.reserve(block.instructions().size() + 8);

		for (const auto & inst: block.instructions()) {
			bool hasUse = false;
			bool hasDef = false;
			for (const auto & operand: inst.operands) {
				hasUse = hasUse || operandUsesVReg(operand, spilledVReg);
				hasDef = hasDef || operandDefsVReg(operand, spilledVReg);
			}

			MachineInstr newInst = inst;
			if (hasUse) {
				MachineOperand reload = MachineOperand::vregDef(function.createVirtualReg(regClass), regClass);
				rewritten.push_back(MachineInstr::make(
					spillLoadOpcode(regClass),
					{reload, MachineOperand::spillSlotOperand(slot)}));
				for (auto & operand: newInst.operands) {
					replaceOperandUse(operand, spilledVReg, reload.asUse());
				}
			}

			MachineOperand newDef;
			if (hasDef) {
				newDef = MachineOperand::vregDef(function.createVirtualReg(regClass), regClass);
				for (auto & operand: newInst.operands) {
					replaceOperandDef(operand, spilledVReg, newDef);
				}
			}

			rewritten.push_back(newInst);

			if (hasDef) {
				rewritten.push_back(MachineInstr::make(
					spillStoreOpcode(regClass),
					{newDef.asUse(), MachineOperand::spillSlotOperand(slot)}));
			}
		}

		block.instructions() = std::move(rewritten);
	}
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
}

void rewriteOperandWithAssignment(MachineOperand & operand, const std::unordered_map<int32_t, PhysicalReg> & assignment)
{
	if (operand.kind == MachineOperandKind::VirtualReg) {
		auto iter = assignment.find(operand.vreg);
		if (iter == assignment.end()) {
			return;
		}
		const MachineOperandRole role = operand.role;
		operand.kind = MachineOperandKind::PhysicalReg;
		operand.role = role;
		operand.preg = iter->second;
		operand.vreg = -1;
		return;
	}

	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		auto iter = assignment.find(operand.memoryBaseVReg);
		if (iter == assignment.end()) {
			return;
		}
		operand.memoryBaseIsPhysical = true;
		operand.memoryBasePreg = iter->second;
		operand.memoryBaseVReg = -1;
	}
}

void applyAssignment(MachineFunction & function, const std::unordered_map<int32_t, PhysicalReg> & assignment)
{
	for (auto & block: function.blocks()) {
		for (auto & inst: block.instructions()) {
			for (auto & operand: inst.operands) {
				rewriteOperandWithAssignment(operand, assignment);
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

bool GraphColoringRegisterAllocator::run(MachineFunction & function, FunctionFrameLayout & layout) const
{
	MachineLegalizer legalizer(layout);
	bool frameFinalized = false;

	for (int iteration = 0; iteration < maxIterations; ++iteration) {
		legalizer.run(function, true);

		MachineCFGBuilder cfgBuilder;
		cfgBuilder.run(function);
		MachineLivenessAnalysis livenessAnalysis;
		MachineLivenessResult liveness = livenessAnalysis.run(function);
		InterferenceGraph graph = buildInterferenceGraph(function, liveness);

		if (!graph.mustSpill.empty()) {
			if (frameFinalized) {
				return false;
			}
			rewriteSpill(function, layout, highestDegreeNode(graph.mustSpill, graph));
			frameFinalized = false;
			continue;
		}

		ColoringResult coloring = colorGraph(graph);
		if (!coloring.success) {
			if (frameFinalized) {
				return false;
			}
			rewriteSpill(function, layout, coloring.spillCandidate);
			frameFinalized = false;
			continue;
		}

		if (!frameFinalized) {
			rewriteFrameSetup(function, layout);
			legalizer.run(function, false);
			frameFinalized = true;
			continue;
		}

		applyAssignment(function, coloring.assignment);
		return verifyNoVirtualRegs(function);
	}

	return false;
}
