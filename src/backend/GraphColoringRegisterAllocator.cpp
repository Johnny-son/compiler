#include "GraphColoringRegisterAllocator.h"

#include "MachineCFG.h"
#include "MachineLegalizer.h"
#include "MachineLiveness.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

constexpr int maxIterations = 128;

const PhysicalReg allocatableGPRs[] = {
	PhysicalReg::A0,
	PhysicalReg::A1,
	PhysicalReg::A2,
	PhysicalReg::A3,
	PhysicalReg::A4,
	PhysicalReg::A5,
	PhysicalReg::A6,
	PhysicalReg::A7,
	PhysicalReg::T0,
	PhysicalReg::T1,
	PhysicalReg::T2,
	PhysicalReg::T3,
	PhysicalReg::T4,
	PhysicalReg::T5,
	PhysicalReg::T6,
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

const PhysicalReg calleeSavedGPRs[] = {
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

const PhysicalReg allocatableFPRs[] = {
	PhysicalReg::FA0,
	PhysicalReg::FA1,
	PhysicalReg::FA2,
	PhysicalReg::FA3,
	PhysicalReg::FA4,
	PhysicalReg::FA5,
	PhysicalReg::FA6,
	PhysicalReg::FA7,
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

const PhysicalReg calleeSavedFPRs[] = {
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

std::vector<PhysicalReg> allocatableRegsFor(RegisterClass regClass)
{
	if (regClass == RegisterClass::FPR) {
		return std::vector<PhysicalReg>(std::begin(allocatableFPRs), std::end(allocatableFPRs));
	}
	return std::vector<PhysicalReg>(std::begin(allocatableGPRs), std::end(allocatableGPRs));
}

std::vector<PhysicalReg> calleeSavedRegsFor(RegisterClass regClass)
{
	if (regClass == RegisterClass::GPR) {
		return std::vector<PhysicalReg>(std::begin(calleeSavedGPRs), std::end(calleeSavedGPRs));
	}
	return std::vector<PhysicalReg>(std::begin(calleeSavedFPRs), std::end(calleeSavedFPRs));
}

bool isCalleeSavedReg(PhysicalReg reg)
{
	for (PhysicalReg calleeSaved: calleeSavedGPRs) {
		if (calleeSaved == reg) {
			return true;
		}
	}
	for (PhysicalReg calleeSaved: calleeSavedFPRs) {
		if (calleeSaved == reg) {
			return true;
		}
	}
	return false;
}

bool isFPR(PhysicalReg reg)
{
	for (PhysicalReg candidate: allocatableFPRs) {
		if (candidate == reg) {
			return true;
		}
	}
	return reg >= PhysicalReg::FA0 && reg <= PhysicalReg::FS11;
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
	std::map<int32_t, std::set<int32_t>> affinities;
	std::map<int32_t, RegisterClass> classes;
	std::map<int32_t, std::set<PhysicalReg>> forbiddenColors;
	std::map<int32_t, double> spillCosts;
	std::set<int32_t> mustSpill;
	std::set<int32_t> callLive;

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

	void addAffinity(int32_t lhs, int32_t rhs)
	{
		if (lhs < 0 || rhs < 0 || lhs == rhs) {
			return;
		}
		affinities[lhs].insert(rhs);
		affinities[rhs].insert(lhs);
	}

	void forbidColor(int32_t vreg, PhysicalReg reg)
	{
		if (vreg >= 0 && TargetRegisterInfo::isValid(reg)) {
			forbiddenColors[vreg].insert(reg);
		}
	}

	int degree(int32_t vreg) const
	{
		auto iter = neighbors.find(vreg);
		return iter == neighbors.end() ? 0 : static_cast<int>(iter->second.size());
	}

	double spillCost(int32_t vreg) const
	{
		auto iter = spillCosts.find(vreg);
		return iter == spillCosts.end() ? 1.0 : std::max(iter->second, 1.0);
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

void collectPhysicalDefs(const MachineInstr & inst, std::set<PhysicalReg> & defs)
{
	for (const auto & operand: inst.operands) {
		if (operand.kind == MachineOperandKind::PhysicalReg && operand.role == MachineOperandRole::Def) {
			defs.insert(operand.preg);
		}
	}
	defs.insert(inst.implicitDefs.begin(), inst.implicitDefs.end());
}

std::vector<int> estimateLoopDepths(const MachineFunction & function)
{
	std::vector<int> depths(function.blocks().size(), 0);
	for (MachineBlockIndex from = 0; from < function.blocks().size(); ++from) {
		for (MachineBlockIndex to: function.blocks()[from].successors()) {
			if (to > from) {
				continue;
			}
			for (MachineBlockIndex index = to; index <= from && index < depths.size(); ++index) {
				++depths[index];
			}
		}
	}
	return depths;
}

double blockWeight(int loopDepth)
{
	double weight = 1.0;
	const int cappedDepth = std::min(loopDepth, 6);
	for (int depth = 0; depth < cappedDepth; ++depth) {
		weight *= 10.0;
	}
	return weight;
}

void addOperandSpillCost(const MachineOperand & operand, double weight, InterferenceGraph & graph)
{
	if (operand.kind == MachineOperandKind::VirtualReg) {
		graph.spillCosts[operand.vreg] += weight;
		return;
	}

	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		graph.spillCosts[operand.memoryBaseVReg] += weight;
	}
}

void collectSpillCosts(const MachineFunction & function, InterferenceGraph & graph)
{
	const auto loopDepths = estimateLoopDepths(function);
	for (MachineBlockIndex blockIndex = 0; blockIndex < function.blocks().size(); ++blockIndex) {
		const double weight = blockWeight(loopDepths[blockIndex]);
		for (const auto & inst: function.blocks()[blockIndex].instructions()) {
			for (const auto & operand: inst.operands) {
				addOperandSpillCost(operand, weight, graph);
			}
		}
	}
}

int32_t copySourceForDef(const MachineInstr & inst, int32_t def)
{
	if (inst.opcode != MachineOpcode::COPY || inst.operands.size() < 2) {
		return -1;
	}

	const auto & dst = inst.operands[0];
	const auto & src = inst.operands[1];
	if (dst.kind != MachineOperandKind::VirtualReg || dst.role != MachineOperandRole::Def || dst.vreg != def) {
		return -1;
	}
	if (src.kind != MachineOperandKind::VirtualReg || src.role != MachineOperandRole::Use) {
		return -1;
	}
	return src.vreg;
}

std::optional<std::pair<int32_t, int32_t>> copyVRegPair(const MachineFunction & function, const MachineInstr & inst)
{
	if (inst.opcode != MachineOpcode::COPY || inst.operands.size() < 2) {
		return std::nullopt;
	}

	const auto & dst = inst.operands[0];
	const auto & src = inst.operands[1];
	if (dst.kind != MachineOperandKind::VirtualReg || dst.role != MachineOperandRole::Def ||
		src.kind != MachineOperandKind::VirtualReg || src.role != MachineOperandRole::Use) {
		return std::nullopt;
	}
	if (classOf(function, dst.vreg) != classOf(function, src.vreg)) {
		return std::nullopt;
	}
	return std::make_pair(dst.vreg, src.vreg);
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

			if (auto copyPair = copyVRegPair(function, inst)) {
				graph.addAffinity(copyPair->first, copyPair->second);
			}

			for (int32_t def: info.def) {
				graph.addNode(def, classOf(function, def));
				const int32_t coalescableCopySource = copySourceForDef(inst, def);
				for (int32_t live: info.liveOut) {
					if (live == coalescableCopySource && classOf(function, live) == classOf(function, def)) {
						continue;
					}
					graph.addEdge(def, live);
				}
			}

			std::set<PhysicalReg> physicalDefs;
			collectPhysicalDefs(inst, physicalDefs);
			for (PhysicalReg reg: physicalDefs) {
				for (int32_t live: info.liveOut) {
					graph.forbidColor(live, reg);
				}
			}

			if (inst.opcode == MachineOpcode::CALL) {
				for (int32_t live: info.liveOut) {
					graph.callLive.insert(live);
				}
			}
		}
	}

	collectSpillCosts(function, graph);
	return graph;
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

int colorCountFor(int32_t vreg, const InterferenceGraph & graph)
{
	auto classIter = graph.classes.find(vreg);
	const RegisterClass regClass = classIter != graph.classes.end() ? classIter->second : RegisterClass::GPR;
	if (graph.callLive.find(vreg) != graph.callLive.end()) {
		return static_cast<int>(calleeSavedRegsFor(regClass).size());
	}
	return static_cast<int>(allocatableRegsFor(regClass).size());
}

std::vector<PhysicalReg> candidateRegsFor(int32_t vreg, const InterferenceGraph & graph)
{
	auto classIter = graph.classes.find(vreg);
	const RegisterClass regClass = classIter != graph.classes.end() ? classIter->second : RegisterClass::GPR;
	auto candidates =
		graph.callLive.find(vreg) != graph.callLive.end() ? calleeSavedRegsFor(regClass) : allocatableRegsFor(regClass);
	auto forbiddenIter = graph.forbiddenColors.find(vreg);
	if (forbiddenIter == graph.forbiddenColors.end()) {
		return candidates;
	}

	std::vector<PhysicalReg> filtered;
	filtered.reserve(candidates.size());
	for (PhysicalReg reg: candidates) {
		if (forbiddenIter->second.find(reg) == forbiddenIter->second.end()) {
			filtered.push_back(reg);
		}
	}
	return filtered;
}

int32_t lowestSpillPriorityNode(
	const std::set<int32_t> & nodes,
	const std::map<int32_t, int> & degrees,
	const InterferenceGraph & graph)
{
	int32_t best = -1;
	double bestPriority = std::numeric_limits<double>::infinity();
	int bestDegree = -1;
	for (int32_t node: nodes) {
		auto degreeIter = degrees.find(node);
		const int degree = std::max(1, degreeIter == degrees.end() ? graph.degree(node) : degreeIter->second);
		const double priority = graph.spillCost(node) / static_cast<double>(degree);
		if (priority < bestPriority ||
			(priority == bestPriority && (degree > bestDegree || (degree == bestDegree && (best < 0 || node < best))))) {
			best = node;
			bestPriority = priority;
			bestDegree = degree;
		}
	}
	return best;
}

std::vector<int32_t> spillBatchForColoringFailure(const InterferenceGraph & graph, int32_t primary)
{
	std::vector<int32_t> batch;
	if (primary >= 0) {
		batch.push_back(primary);
	}

	const std::size_t nodeCount = graph.neighbors.size();
	if (nodeCount < 200) {
		return batch;
	}

	std::vector<int32_t> candidates;
	candidates.reserve(nodeCount);
	for (const auto & entry: graph.neighbors) {
		if (entry.first != primary) {
			candidates.push_back(entry.first);
		}
	}

	std::sort(candidates.begin(), candidates.end(), [&graph](int32_t lhs, int32_t rhs) {
		const int lhsDegree = std::max(1, graph.degree(lhs));
		const int rhsDegree = std::max(1, graph.degree(rhs));
		const double lhsPriority = graph.spillCost(lhs) / static_cast<double>(lhsDegree);
		const double rhsPriority = graph.spillCost(rhs) / static_cast<double>(rhsDegree);
		if (lhsPriority != rhsPriority) {
			return lhsPriority < rhsPriority;
		}
		if (lhsDegree != rhsDegree) {
			return lhsDegree > rhsDegree;
		}
		return lhs < rhs;
	});

	const std::size_t targetSize = std::min<std::size_t>(32, std::max<std::size_t>(4, nodeCount / 32));
	for (int32_t candidate: candidates) {
		if (batch.size() >= targetSize) {
			break;
		}
		batch.push_back(candidate);
	}
	return batch;
}

ColoringResult colorGraph(const InterferenceGraph & graph)
{
	ColoringResult result;
	std::set<int32_t> remaining;
	std::map<int32_t, int> degrees;
	std::set<std::pair<int, int32_t>> lowDegreeNodes;
	std::vector<int32_t> selectStack;

	for (const auto & entry: graph.neighbors) {
		const int32_t node = entry.first;
		const int degree = static_cast<int>(entry.second.size());
		remaining.insert(node);
		degrees[node] = degree;
		if (degree < colorCountFor(node, graph)) {
			lowDegreeNodes.emplace(degree, node);
		}
	}
	selectStack.reserve(remaining.size());

	while (!remaining.empty()) {
		int32_t selected = -1;

		if (!lowDegreeNodes.empty()) {
			selected = lowDegreeNodes.begin()->second;
			lowDegreeNodes.erase(lowDegreeNodes.begin());
		} else {
			selected = lowestSpillPriorityNode(remaining, degrees, graph);
		}

		selectStack.push_back(selected);
		remaining.erase(selected);
		auto neighborIter = graph.neighbors.find(selected);
		if (neighborIter == graph.neighbors.end()) {
			continue;
		}

		for (int32_t neighbor: neighborIter->second) {
			if (remaining.find(neighbor) == remaining.end()) {
				continue;
			}
			const int oldDegree = degrees[neighbor];
			lowDegreeNodes.erase(std::make_pair(oldDegree, neighbor));
			const int newDegree = oldDegree - 1;
			degrees[neighbor] = newDegree;
			if (newDegree < colorCountFor(neighbor, graph)) {
				lowDegreeNodes.emplace(newDegree, neighbor);
			}
		}
	}

	while (!selectStack.empty()) {
		const int32_t node = selectStack.back();
		selectStack.pop_back();

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
		const auto candidates = candidateRegsFor(node, graph);
		auto affinityIter = graph.affinities.find(node);
		if (affinityIter != graph.affinities.end()) {
			for (int32_t partner: affinityIter->second) {
				auto assigned = result.assignment.find(partner);
				if (assigned == result.assignment.end()) {
					continue;
				}
				if (used.find(assigned->second) == used.end() &&
					std::find(candidates.begin(), candidates.end(), assigned->second) != candidates.end()) {
					selected = assigned->second;
					break;
				}
			}
		}
		for (PhysicalReg reg: candidates) {
			if (selected.has_value()) {
				break;
			}
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

void rewriteSpills(MachineFunction & function, FunctionFrameLayout & layout, const std::vector<int32_t> & spilledVRegs)
{
	for (int32_t spilledVReg: spilledVRegs) {
		rewriteSpill(function, layout, spilledVReg);
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

std::vector<PhysicalReg> usedCalleeSavedRegs(const std::unordered_map<int32_t, PhysicalReg> & assignment)
{
	std::set<PhysicalReg> used;
	for (const auto & entry: assignment) {
		if (isCalleeSavedReg(entry.second)) {
			used.insert(entry.second);
		}
	}

	std::vector<PhysicalReg> ordered;
	for (PhysicalReg reg: calleeSavedGPRs) {
		if (used.find(reg) != used.end()) {
			ordered.push_back(reg);
		}
	}
	for (PhysicalReg reg: calleeSavedFPRs) {
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

std::map<PhysicalReg, int32_t> createCalleeSavedSlots(
	FunctionFrameLayout & layout,
	const std::vector<PhysicalReg> & regs)
{
	std::map<PhysicalReg, int32_t> slots;
	for (PhysicalReg reg: regs) {
		slots.emplace(reg, layout.createSpillSlot(8, 8));
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
	saves.reserve(slots.size());
	for (const auto & entry: slots) {
		const MachineOpcode opcode = isFPR(entry.first) ? MachineOpcode::FSW : MachineOpcode::SD;
		saves.push_back(MachineInstr::make(
			opcode,
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
	restores.reserve(slots.size());
	for (auto iter = slots.rbegin(); iter != slots.rend(); ++iter) {
		const MachineOpcode opcode = isFPR(iter->first) ? MachineOpcode::FLW : MachineOpcode::LD;
		restores.push_back(MachineInstr::make(
			opcode,
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

void preserveCalleeSavedRegisters(
	MachineFunction & function,
	FunctionFrameLayout & layout,
	const std::unordered_map<int32_t, PhysicalReg> & assignment)
{
	const auto used = usedCalleeSavedRegs(assignment);
	if (used.empty()) {
		return;
	}

	const auto slots = createCalleeSavedSlots(layout, used);
	insertCalleeSavedPrologue(function, slots);
	insertCalleeSavedEpilogues(function, slots);
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
			std::vector<int32_t> spillCandidates;
			spillCandidates.reserve(graph.mustSpill.size());
			for (int32_t vreg: graph.mustSpill) {
				spillCandidates.push_back(vreg);
			}
			std::sort(spillCandidates.begin(), spillCandidates.end(), [&graph](int32_t lhs, int32_t rhs) {
				const int lhsDegree = graph.degree(lhs);
				const int rhsDegree = graph.degree(rhs);
				if (lhsDegree != rhsDegree) {
					return lhsDegree > rhsDegree;
				}
				return lhs < rhs;
			});
			rewriteSpills(function, layout, spillCandidates);
			frameFinalized = false;
			continue;
		}

		ColoringResult coloring = colorGraph(graph);
		if (!coloring.success) {
			if (frameFinalized) {
				return false;
			}
			rewriteSpills(function, layout, spillBatchForColoringFailure(graph, coloring.spillCandidate));
			frameFinalized = false;
			continue;
		}

		if (!frameFinalized) {
			preserveCalleeSavedRegisters(function, layout, coloring.assignment);
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
