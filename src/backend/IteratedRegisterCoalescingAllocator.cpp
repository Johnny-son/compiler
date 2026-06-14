#include "IteratedRegisterCoalescingAllocator.h"

#include "MachineCFG.h"
#include "MachineLegalizer.h"
#include "MachineLiveness.h"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int maxIterations = 128;
constexpr std::size_t maxSpillBatchSize = 8;
constexpr const char * defaultDebugDir = "/home/ubuntu/桌面/compiler-workspace/debug";

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

const PhysicalReg callerSavedGPRs[] = {
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

const PhysicalReg callerSavedFPRs[] = {
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
	if (regClass == RegisterClass::FPR) {
		return std::vector<PhysicalReg>(std::begin(calleeSavedFPRs), std::end(calleeSavedFPRs));
	}
	return std::vector<PhysicalReg>(std::begin(calleeSavedGPRs), std::end(calleeSavedGPRs));
}

std::vector<PhysicalReg> callerSavedRegsFor(RegisterClass regClass)
{
	if (regClass == RegisterClass::FPR) {
		return std::vector<PhysicalReg>(std::begin(callerSavedFPRs), std::end(callerSavedFPRs));
	}
	return std::vector<PhysicalReg>(std::begin(callerSavedGPRs), std::end(callerSavedGPRs));
}

bool containsReg(const std::vector<PhysicalReg> & regs, PhysicalReg reg)
{
	return std::find(regs.begin(), regs.end(), reg) != regs.end();
}

bool isAllocatableReg(PhysicalReg reg)
{
	return containsReg(allocatableRegsFor(RegisterClass::GPR), reg) ||
		   containsReg(allocatableRegsFor(RegisterClass::FPR), reg);
}

bool isCalleeSavedReg(PhysicalReg reg)
{
	return containsReg(calleeSavedRegsFor(RegisterClass::GPR), reg) ||
		   containsReg(calleeSavedRegsFor(RegisterClass::FPR), reg);
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

RegisterClass classOfPhysical(PhysicalReg reg)
{
	return isFPR(reg) ? RegisterClass::FPR : RegisterClass::GPR;
}

MachineOpcode spillLoadOpcode(RegisterClass regClass)
{
	return regClass == RegisterClass::FPR ? MachineOpcode::FLW : MachineOpcode::LD;
}

MachineOpcode spillStoreOpcode(RegisterClass regClass)
{
	return regClass == RegisterClass::FPR ? MachineOpcode::FSW : MachineOpcode::SD;
}

struct IRCNode {
	enum class Kind : std::int8_t {
		Virtual,
		Physical
	};

	Kind kind = Kind::Virtual;
	int32_t vreg = -1;
	PhysicalReg preg = PhysicalReg::Invalid;

	static IRCNode virtualReg(int32_t vreg)
	{
		IRCNode node;
		node.kind = Kind::Virtual;
		node.vreg = vreg;
		return node;
	}

	static IRCNode physicalReg(PhysicalReg preg)
	{
		IRCNode node;
		node.kind = Kind::Physical;
		node.preg = preg;
		return node;
	}

	[[nodiscard]] bool isVirtual() const
	{
		return kind == Kind::Virtual;
	}

	[[nodiscard]] bool isPhysical() const
	{
		return kind == Kind::Physical;
	}
};

bool operator<(const IRCNode & lhs, const IRCNode & rhs)
{
	return std::tie(lhs.kind, lhs.vreg, lhs.preg) < std::tie(rhs.kind, rhs.vreg, rhs.preg);
}

bool operator==(const IRCNode & lhs, const IRCNode & rhs)
{
	return lhs.kind == rhs.kind && lhs.vreg == rhs.vreg && lhs.preg == rhs.preg;
}

using MoveId = std::size_t;

struct IRCMove {
	IRCNode dst;
	IRCNode src;
};

struct IRCState {
	std::set<IRCNode> precolored;
	std::set<IRCNode> initial;
	std::set<IRCNode> activeNodes;

	std::set<IRCNode> simplifyWorklist;
	std::set<IRCNode> freezeWorklist;
	std::set<IRCNode> spillWorklist;

	std::set<IRCNode> spilledNodes;
	std::set<IRCNode> coalescedNodes;
	std::set<IRCNode> coloredNodes;
	std::vector<IRCNode> selectStack;

	std::set<std::pair<IRCNode, IRCNode>> adjSet;
	std::map<IRCNode, std::set<IRCNode>> adjList;
	std::map<IRCNode, int> degree;

	std::vector<IRCMove> moves;
	std::map<IRCNode, std::set<MoveId>> moveList;
	std::map<IRCNode, IRCNode> alias;

	std::set<MoveId> worklistMoves;
	std::set<MoveId> activeMoves;
	std::set<MoveId> coalescedMoves;
	std::set<MoveId> constrainedMoves;
	std::set<MoveId> frozenMoves;

	std::map<IRCNode, RegisterClass> classes;
	std::map<IRCNode, PhysicalReg> color;
	std::map<IRCNode, double> spillCosts;
};

struct RADebugIteration {
	int iteration = 0;
	std::size_t blocks = 0;
	std::size_t instructions = 0;
	std::size_t virtualRegs = 0;
	std::size_t initialNodes = 0;
	std::size_t interferenceEdges = 0;
	std::size_t moveCount = 0;
	bool colored = false;
	std::vector<int32_t> spilledVRegs;
};

struct RADebugContext {
	bool enabled = false;
	std::filesystem::path dir;
	std::vector<RADebugIteration> iterations;
	std::vector<PhysicalReg> savedCalleeRegs;
	std::vector<PhysicalReg> finalCalleeRegs;
	std::unordered_map<int32_t, PhysicalReg> finalAssignment;
	bool success = false;
	bool verifyOk = false;
	bool hitIterationLimit = false;
	std::string failureReason;
};

std::pair<IRCNode, IRCNode> orderedEdge(IRCNode lhs, IRCNode rhs)
{
	if (rhs < lhs) {
		std::swap(lhs, rhs);
	}
	return std::make_pair(lhs, rhs);
}

RegisterClass classOf(const IRCState & state, IRCNode node)
{
	auto iter = state.classes.find(node);
	if (iter != state.classes.end()) {
		return iter->second;
	}
	if (node.isPhysical()) {
		return classOfPhysical(node.preg);
	}
	return RegisterClass::GPR;
}

int colorCountFor(const IRCState & state, IRCNode node)
{
	return static_cast<int>(allocatableRegsFor(classOf(state, node)).size());
}

bool debugEnabledByEnv()
{
	const char * dir = std::getenv("MINIC_BACKEND_RA_DEBUG_DIR");
	if (dir != nullptr && dir[0] != '\0') {
		return true;
	}
	const char * enabled = std::getenv("MINIC_BACKEND_RA_DEBUG");
	return enabled != nullptr && enabled[0] != '\0' && std::string(enabled) != "0";
}

bool timingEnabledByEnv()
{
	const char * enabled = std::getenv("MINIC_BACKEND_TIMING");
	return enabled != nullptr && enabled[0] != '\0' && std::string(enabled) != "0";
}

std::filesystem::path debugDirFromEnv()
{
	const char * dir = std::getenv("MINIC_BACKEND_RA_DEBUG_DIR");
	if (dir != nullptr && dir[0] != '\0') {
		return std::filesystem::path(dir);
	}
	return std::filesystem::path(defaultDebugDir);
}

std::string safeFileName(std::string name)
{
	if (name.empty()) {
		return "function";
	}
	for (char & ch: name) {
		const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
						ch == '_' || ch == '-' || ch == '.';
		if (!ok) {
			ch = '_';
		}
	}
	return name;
}

std::string joinRegs(const std::vector<PhysicalReg> & regs)
{
	if (regs.empty()) {
		return "-";
	}
	std::ostringstream out;
	for (std::size_t index = 0; index < regs.size(); ++index) {
		if (index != 0) {
			out << " ";
		}
		out << TargetRegisterInfo::name(regs[index]);
	}
	return out.str();
}

std::string joinVRegs(const std::vector<int32_t> & vregs)
{
	if (vregs.empty()) {
		return "-";
	}
	std::ostringstream out;
	for (std::size_t index = 0; index < vregs.size(); ++index) {
		if (index != 0) {
			out << " ";
		}
		out << "%" << vregs[index];
	}
	return out.str();
}

std::size_t instructionCount(const MachineFunction & function)
{
	std::size_t count = 0;
	for (const auto & block: function.blocks()) {
		count += block.instructions().size();
	}
	return count;
}

std::size_t maxReferencedVirtualReg(const MachineFunction & function)
{
	int32_t maxVReg = -1;
	for (const auto & block: function.blocks()) {
		for (const auto & inst: block.instructions()) {
			for (const auto & operand: inst.operands) {
				if (operand.kind == MachineOperandKind::VirtualReg) {
					maxVReg = std::max(maxVReg, operand.vreg);
				} else if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
					maxVReg = std::max(maxVReg, operand.memoryBaseVReg);
				}
			}
		}
	}
	return maxVReg < 0 ? 0 : static_cast<std::size_t>(maxVReg + 1);
}

std::size_t spillSlotCount(const FunctionFrameLayout & layout)
{
	std::size_t count = 0;
	for (const auto & slot: layout.slots()) {
		if (slot.kind == StackObjectKind::SpillSlot) {
			++count;
		}
	}
	return count;
}

void countFinalOpcodes(
	const MachineFunction & function,
	std::map<MachineOpcode, std::size_t> & opcodeCounts,
	std::size_t & memoryOps,
	std::size_t & copies)
{
	for (const auto & block: function.blocks()) {
		for (const auto & inst: block.instructions()) {
			++opcodeCounts[inst.opcode];
			switch (inst.opcode) {
				case MachineOpcode::LW:
				case MachineOpcode::LD:
				case MachineOpcode::FLW:
				case MachineOpcode::SW:
				case MachineOpcode::SD:
				case MachineOpcode::FSW:
					++memoryOps;
					break;
				case MachineOpcode::COPY:
					++copies;
					break;
				default:
					break;
			}
		}
	}
}

double spillCost(const IRCState & state, IRCNode node)
{
	auto iter = state.spillCosts.find(node);
	return iter == state.spillCosts.end() ? 1.0 : std::max(iter->second, 1.0);
}

int currentDegree(const IRCState & state, IRCNode node)
{
	auto iter = state.degree.find(node);
	return iter == state.degree.end() ? 0 : iter->second;
}

void addVirtualNode(IRCState & state, const MachineFunction & function, int32_t vreg)
{
	if (vreg < 0) {
		return;
	}

	IRCNode node = IRCNode::virtualReg(vreg);
	state.initial.insert(node);
	state.adjList.try_emplace(node);
	state.degree.try_emplace(node, 0);
	state.classes[node] = function.registerClass(vreg);
}

void addPrecoloredNode(IRCState & state, PhysicalReg reg)
{
	if (!isAllocatableReg(reg)) {
		return;
	}

	IRCNode node = IRCNode::physicalReg(reg);
	state.precolored.insert(node);
	state.adjList.try_emplace(node);
	state.classes[node] = classOfPhysical(reg);
	state.color[node] = reg;
}

void addEdge(IRCState & state, IRCNode lhs, IRCNode rhs)
{
	if (lhs == rhs || lhs.isPhysical() && rhs.isPhysical()) {
		return;
	}
	if (classOf(state, lhs) != classOf(state, rhs)) {
		return;
	}

	const auto edge = orderedEdge(lhs, rhs);
	if (!state.adjSet.insert(edge).second) {
		return;
	}

	state.adjList[lhs].insert(rhs);
	state.adjList[rhs].insert(lhs);
	if (lhs.isVirtual()) {
		++state.degree[lhs];
	}
	if (rhs.isVirtual()) {
		++state.degree[rhs];
	}
}

void collectOperandNode(IRCState & state, const MachineFunction & function, const MachineOperand & operand)
{
	if (operand.kind == MachineOperandKind::VirtualReg) {
		addVirtualNode(state, function, operand.vreg);
		return;
	}

	if (operand.kind == MachineOperandKind::PhysicalReg) {
		addPrecoloredNode(state, operand.preg);
		return;
	}

	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		addVirtualNode(state, function, operand.memoryBaseVReg);
	}
}

std::optional<IRCNode> nodeForRegOperand(
	IRCState & state,
	const MachineFunction & function,
	const MachineOperand & operand)
{
	if (operand.kind == MachineOperandKind::VirtualReg) {
		addVirtualNode(state, function, operand.vreg);
		return IRCNode::virtualReg(operand.vreg);
	}
	if (operand.kind == MachineOperandKind::PhysicalReg && isAllocatableReg(operand.preg)) {
		addPrecoloredNode(state, operand.preg);
		return IRCNode::physicalReg(operand.preg);
	}
	return std::nullopt;
}

void collectMove(IRCState & state, const MachineFunction & function, const MachineInstr & inst)
{
	if (inst.opcode != MachineOpcode::COPY || inst.operands.size() < 2) {
		return;
	}

	auto dst = nodeForRegOperand(state, function, inst.operands[0]);
	auto src = nodeForRegOperand(state, function, inst.operands[1]);
	if (!dst.has_value() || !src.has_value() || *dst == *src || classOf(state, *dst) != classOf(state, *src)) {
		return;
	}

	const MoveId id = state.moves.size();
	state.moves.push_back(IRCMove{*dst, *src});
	state.moveList[*dst].insert(id);
	state.moveList[*src].insert(id);
	state.worklistMoves.insert(id);
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

void addOperandSpillCost(
	const MachineFunction & function,
	const MachineOperand & operand,
	double weight,
	IRCState & state)
{
	if (operand.kind == MachineOperandKind::VirtualReg) {
		IRCNode node = IRCNode::virtualReg(operand.vreg);
		addVirtualNode(state, function, operand.vreg);
		state.spillCosts[node] += weight;
		return;
	}

	if (operand.kind == MachineOperandKind::Memory && !operand.memoryBaseIsPhysical) {
		IRCNode node = IRCNode::virtualReg(operand.memoryBaseVReg);
		addVirtualNode(state, function, operand.memoryBaseVReg);
		state.spillCosts[node] += weight;
	}
}

void collectSpillCosts(const MachineFunction & function, IRCState & state)
{
	const auto loopDepths = estimateLoopDepths(function);
	for (MachineBlockIndex blockIndex = 0; blockIndex < function.blocks().size(); ++blockIndex) {
		const double weight = blockWeight(loopDepths[blockIndex]);
		for (const auto & inst: function.blocks()[blockIndex].instructions()) {
			for (const auto & operand: inst.operands) {
				addOperandSpillCost(function, operand, weight, state);
			}
		}
	}
}

void initializePrecolored(IRCState & state)
{
	for (PhysicalReg reg: allocatableRegsFor(RegisterClass::GPR)) {
		addPrecoloredNode(state, reg);
	}
	for (PhysicalReg reg: allocatableRegsFor(RegisterClass::FPR)) {
		addPrecoloredNode(state, reg);
	}
}

void addPhysicalClobberEdges(IRCState & state, const MachineLivenessResult & liveness, MachineBlockIndex blockIndex, std::size_t instIndex, PhysicalReg reg)
{
	if (!isAllocatableReg(reg)) {
		return;
	}

	IRCNode pregNode = IRCNode::physicalReg(reg);
	const RegisterClass regClass = classOfPhysical(reg);
	for (int32_t live: liveness.instruction(blockIndex, instIndex).liveOut) {
		IRCNode liveNode = IRCNode::virtualReg(live);
		if (classOf(state, liveNode) == regClass) {
			addEdge(state, liveNode, pregNode);
		}
	}
}

IRCState buildIRCState(const MachineFunction & function, const MachineLivenessResult & liveness)
{
	IRCState state;
	initializePrecolored(state);

	for (MachineBlockIndex blockIndex = 0; blockIndex < function.blocks().size(); ++blockIndex) {
		const auto & block = function.blocks()[blockIndex];
		for (std::size_t instIndex = 0; instIndex < block.instructions().size(); ++instIndex) {
			const auto & inst = block.instructions()[instIndex];
			const auto & info = liveness.instruction(blockIndex, instIndex);

			for (const auto & operand: inst.operands) {
				collectOperandNode(state, function, operand);
			}
			for (int32_t vreg: info.liveIn) {
				addVirtualNode(state, function, vreg);
			}
			for (int32_t vreg: info.liveOut) {
				addVirtualNode(state, function, vreg);
			}

			collectMove(state, function, inst);

			for (int32_t def: info.def) {
				IRCNode defNode = IRCNode::virtualReg(def);
				addVirtualNode(state, function, def);
				for (int32_t live: info.liveOut) {
					IRCNode liveNode = IRCNode::virtualReg(live);
					if (classOf(state, defNode) == classOf(state, liveNode)) {
						addEdge(state, defNode, liveNode);
					}
				}
			}

			std::set<PhysicalReg> physicalDefs;
			collectPhysicalDefs(inst, physicalDefs);
			for (PhysicalReg reg: physicalDefs) {
				addPhysicalClobberEdges(state, liveness, blockIndex, instIndex, reg);
			}

			if (inst.opcode == MachineOpcode::CALL) {
				for (PhysicalReg reg: callerSavedRegsFor(RegisterClass::GPR)) {
					addPhysicalClobberEdges(state, liveness, blockIndex, instIndex, reg);
				}
				for (PhysicalReg reg: callerSavedRegsFor(RegisterClass::FPR)) {
					addPhysicalClobberEdges(state, liveness, blockIndex, instIndex, reg);
				}
			}
		}
	}

	collectSpillCosts(function, state);
	return state;
}

void makeWorklist(IRCState & state)
{
	for (IRCNode node: state.initial) {
		state.activeNodes.insert(node);
		if (currentDegree(state, node) < colorCountFor(state, node)) {
			state.simplifyWorklist.insert(node);
		} else {
			state.spillWorklist.insert(node);
		}
	}
	state.initial.clear();
}

void decrementDegree(IRCState & state, IRCNode node)
{
	const int oldDegree = currentDegree(state, node);
	const int newDegree = oldDegree - 1;
	state.degree[node] = newDegree;
	if (oldDegree >= colorCountFor(state, node) && newDegree < colorCountFor(state, node)) {
		auto iter = state.spillWorklist.find(node);
		if (iter != state.spillWorklist.end()) {
			state.spillWorklist.erase(iter);
			state.simplifyWorklist.insert(node);
		}
	}
}

void simplifyOne(IRCState & state)
{
	if (state.simplifyWorklist.empty()) {
		return;
	}

	IRCNode node = *state.simplifyWorklist.begin();
	state.simplifyWorklist.erase(state.simplifyWorklist.begin());
	state.activeNodes.erase(node);
	state.selectStack.push_back(node);

	auto neighborIter = state.adjList.find(node);
	if (neighborIter == state.adjList.end()) {
		return;
	}

	for (IRCNode neighbor: neighborIter->second) {
		if (neighbor.isVirtual() && state.activeNodes.find(neighbor) != state.activeNodes.end()) {
			decrementDegree(state, neighbor);
		}
	}
}

IRCNode lowestSpillPriorityNode(const std::set<IRCNode> & nodes, const IRCState & state)
{
	IRCNode best;
	bool hasBest = false;
	double bestPriority = std::numeric_limits<double>::infinity();
	int bestDegree = -1;
	for (IRCNode node: nodes) {
		const int degree = std::max(1, currentDegree(state, node));
		const double priority = spillCost(state, node) / static_cast<double>(degree);
		if (!hasBest || priority < bestPriority ||
			(priority == bestPriority &&
			 (degree > bestDegree || (degree == bestDegree && node < best)))) {
			best = node;
			hasBest = true;
			bestPriority = priority;
			bestDegree = degree;
		}
	}
	return best;
}

void selectSpill(IRCState & state)
{
	if (state.spillWorklist.empty()) {
		return;
	}

	IRCNode node = lowestSpillPriorityNode(state.spillWorklist, state);
	state.spillWorklist.erase(node);
	state.simplifyWorklist.insert(node);
}

void runWorklists(IRCState & state)
{
	makeWorklist(state);
	while (!state.simplifyWorklist.empty() || !state.spillWorklist.empty()) {
		if (!state.simplifyWorklist.empty()) {
			simplifyOne(state);
		} else {
			selectSpill(state);
		}
	}
}

std::vector<PhysicalReg> candidateRegsFor(const IRCState & state, IRCNode node)
{
	return allocatableRegsFor(classOf(state, node));
}

bool assignColors(IRCState & state)
{
	while (!state.selectStack.empty()) {
		IRCNode node = state.selectStack.back();
		state.selectStack.pop_back();

		std::set<PhysicalReg> used;
		auto neighborIter = state.adjList.find(node);
		if (neighborIter != state.adjList.end()) {
			for (IRCNode neighbor: neighborIter->second) {
				auto colorIter = state.color.find(neighbor);
				if (colorIter != state.color.end()) {
					used.insert(colorIter->second);
				}
			}
		}

		std::optional<PhysicalReg> selected;
		for (PhysicalReg reg: candidateRegsFor(state, node)) {
			if (used.find(reg) == used.end()) {
				selected = reg;
				break;
			}
		}

		if (!selected.has_value()) {
			state.spilledNodes.insert(node);
			continue;
		}

		state.coloredNodes.insert(node);
		state.color[node] = *selected;
	}

	return state.spilledNodes.empty();
}

std::unordered_map<int32_t, PhysicalReg> exportAssignment(const IRCState & state)
{
	std::unordered_map<int32_t, PhysicalReg> assignment;
	for (const auto & entry: state.color) {
		if (entry.first.isVirtual()) {
			assignment[entry.first.vreg] = entry.second;
		}
	}
	return assignment;
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

std::vector<int32_t> spillBatch(const IRCState & state)
{
	std::vector<IRCNode> nodes;
	nodes.reserve(state.spilledNodes.size());
	for (IRCNode node: state.spilledNodes) {
		if (node.isVirtual()) {
			nodes.push_back(node);
		}
	}

	std::sort(nodes.begin(), nodes.end(), [&state](IRCNode lhs, IRCNode rhs) {
		const int lhsDegree = std::max(1, currentDegree(state, lhs));
		const int rhsDegree = std::max(1, currentDegree(state, rhs));
		const double lhsPriority = spillCost(state, lhs) / static_cast<double>(lhsDegree);
		const double rhsPriority = spillCost(state, rhs) / static_cast<double>(rhsDegree);
		if (lhsPriority != rhsPriority) {
			return lhsPriority < rhsPriority;
		}
		if (lhsDegree != rhsDegree) {
			return lhsDegree > rhsDegree;
		}
		return lhs < rhs;
	});

	std::vector<int32_t> batch;
	for (IRCNode node: nodes) {
		if (batch.size() >= maxSpillBatchSize) {
			break;
		}
		batch.push_back(node.vreg);
	}
	return batch;
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

std::vector<PhysicalReg> missingCalleeSavedRegs(
	const std::vector<PhysicalReg> & required,
	const std::vector<PhysicalReg> & saved)
{
	std::set<PhysicalReg> savedSet(saved.begin(), saved.end());
	std::vector<PhysicalReg> missing;
	for (PhysicalReg reg: required) {
		if (savedSet.find(reg) == savedSet.end()) {
			missing.push_back(reg);
		}
	}
	return missing;
}

void appendUniqueRegs(std::vector<PhysicalReg> & dst, const std::vector<PhysicalReg> & regs)
{
	std::set<PhysicalReg> present(dst.begin(), dst.end());
	for (PhysicalReg reg: regs) {
		if (present.insert(reg).second) {
			dst.push_back(reg);
		}
	}
}

bool sameRegSet(const std::vector<PhysicalReg> & lhs, const std::vector<PhysicalReg> & rhs)
{
	return std::set<PhysicalReg>(lhs.begin(), lhs.end()) == std::set<PhysicalReg>(rhs.begin(), rhs.end());
}

std::string opcodeDebugName(MachineOpcode opcode)
{
	switch (opcode) {
		case MachineOpcode::ADDI: return "ADDI";
		case MachineOpcode::ADDIW: return "ADDIW";
		case MachineOpcode::ADD: return "ADD";
		case MachineOpcode::ADDW: return "ADDW";
		case MachineOpcode::SUBW: return "SUBW";
		case MachineOpcode::MUL: return "MUL";
		case MachineOpcode::MULW: return "MULW";
		case MachineOpcode::REM: return "REM";
		case MachineOpcode::DIVW: return "DIVW";
		case MachineOpcode::REMW: return "REMW";
		case MachineOpcode::SLLI: return "SLLI";
		case MachineOpcode::AND: return "AND";
		case MachineOpcode::OR: return "OR";
		case MachineOpcode::XOR: return "XOR";
		case MachineOpcode::SLT: return "SLT";
		case MachineOpcode::XORI: return "XORI";
		case MachineOpcode::ANDI: return "ANDI";
		case MachineOpcode::SEQZ: return "SEQZ";
		case MachineOpcode::SNEZ: return "SNEZ";
		case MachineOpcode::LI: return "LI";
		case MachineOpcode::LA: return "LA";
		case MachineOpcode::LA_STACK: return "LA_STACK";
		case MachineOpcode::LW: return "LW";
		case MachineOpcode::LD: return "LD";
		case MachineOpcode::FLW: return "FLW";
		case MachineOpcode::SW: return "SW";
		case MachineOpcode::SD: return "SD";
		case MachineOpcode::FSW: return "FSW";
		case MachineOpcode::FADD_S: return "FADD_S";
		case MachineOpcode::FSUB_S: return "FSUB_S";
		case MachineOpcode::FMUL_S: return "FMUL_S";
		case MachineOpcode::FDIV_S: return "FDIV_S";
		case MachineOpcode::FEQ_S: return "FEQ_S";
		case MachineOpcode::FLT_S: return "FLT_S";
		case MachineOpcode::FLE_S: return "FLE_S";
		case MachineOpcode::FCVT_S_W: return "FCVT_S_W";
		case MachineOpcode::FCVT_W_S: return "FCVT_W_S";
		case MachineOpcode::FMV_W_X: return "FMV_W_X";
		case MachineOpcode::FMV_X_W: return "FMV_X_W";
		case MachineOpcode::COPY: return "COPY";
		case MachineOpcode::CALL: return "CALL";
		case MachineOpcode::J: return "J";
		case MachineOpcode::BEQ: return "BEQ";
		case MachineOpcode::BNE: return "BNE";
		case MachineOpcode::BLT: return "BLT";
		case MachineOpcode::BGE: return "BGE";
		case MachineOpcode::BNEZ: return "BNEZ";
		case MachineOpcode::RET: return "RET";
		default:
			return std::to_string(static_cast<int>(opcode));
	}
}

std::filesystem::path nextDebugPath(
	const std::filesystem::path & dir,
	const std::string & debugTag,
	const std::string & functionName)
{
	const std::string base = debugTag.empty() ? safeFileName(functionName) : safeFileName(debugTag + "." + functionName);
	auto path = dir / (base + ".ra.txt");
	if (!std::filesystem::exists(path)) {
		return path;
	}
	for (int index = 1; index < 100000; ++index) {
		path = dir / (base + "." + std::to_string(index) + ".ra.txt");
		if (!std::filesystem::exists(path)) {
			return path;
		}
	}
	return dir / (base + ".overflow.ra.txt");
}

void writeRADebugReport(
	const MachineFunction & function,
	const FunctionFrameLayout & layout,
	const RADebugContext & debug,
	const std::string & debugTag)
{
	if (!debug.enabled) {
		return;
	}

	std::error_code error;
	std::filesystem::create_directories(debug.dir, error);
	if (error) {
		return;
	}

	const auto path = nextDebugPath(debug.dir, debugTag, function.name());
	std::ofstream out(path);
	if (!out) {
		return;
	}

	std::map<MachineOpcode, std::size_t> opcodeCounts;
	std::size_t memoryOps = 0;
	std::size_t copies = 0;
	countFinalOpcodes(function, opcodeCounts, memoryOps, copies);

	out << "function: " << function.name() << "\n";
	out << "success: " << (debug.success ? "yes" : "no") << "\n";
	out << "verify_no_virtual_regs: " << (debug.verifyOk ? "yes" : "no") << "\n";
	out << "hit_iteration_limit: " << (debug.hitIterationLimit ? "yes" : "no") << "\n";
	if (!debug.failureReason.empty()) {
		out << "failure_reason: " << debug.failureReason << "\n";
	}
	out << "iterations: " << debug.iterations.size() << "\n";
	out << "final_blocks: " << function.blocks().size() << "\n";
	out << "final_instructions: " << instructionCount(function) << "\n";
	out << "final_referenced_vregs: " << maxReferencedVirtualReg(function) << "\n";
	out << "frame_size: " << layout.frameSize() << "\n";
	out << "spill_slots: " << spillSlotCount(layout) << "\n";
	out << "saved_callee_regs: " << joinRegs(debug.savedCalleeRegs) << "\n";
	out << "final_callee_regs: " << joinRegs(debug.finalCalleeRegs) << "\n";
	out << "callee_saved_match: " << (sameRegSet(debug.savedCalleeRegs, debug.finalCalleeRegs) ? "yes" : "no") << "\n";
	out << "final_memory_ops: " << memoryOps << "\n";
	out << "final_copy_ops: " << copies << "\n";
	out << "\niteration,blocks,instructions,referenced_vregs,initial_nodes,edges,moves,colored,spilled\n";
	for (const auto & iter: debug.iterations) {
		out << iter.iteration << "," << iter.blocks << "," << iter.instructions << "," << iter.virtualRegs << ","
			<< iter.initialNodes << "," << iter.interferenceEdges << "," << iter.moveCount << ","
			<< (iter.colored ? "yes" : "no") << "," << joinVRegs(iter.spilledVRegs) << "\n";
	}
	out << "\nopcode_counts:\n";
	for (const auto & entry: opcodeCounts) {
		out << opcodeDebugName(entry.first) << " " << entry.second << "\n";
	}
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
	const std::vector<PhysicalReg> & regs)
{
	if (regs.empty()) {
		return;
	}

	const auto slots = createCalleeSavedSlots(layout, regs);
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

IteratedRegisterCoalescingAllocator::IteratedRegisterCoalescingAllocator(std::string debugTag)
	: debugTag(std::move(debugTag))
{}

bool IteratedRegisterCoalescingAllocator::run(MachineFunction & function, FunctionFrameLayout & layout) const
{
	MachineLegalizer legalizer(layout);
	bool frameFinalized = false;
	std::vector<PhysicalReg> savedCalleeRegs;
	RADebugContext debug;
	debug.enabled = debugEnabledByEnv();
	debug.dir = debugDirFromEnv();
	const bool timingEnabled = timingEnabledByEnv();

	for (int iteration = 0; iteration < maxIterations; ++iteration) {
		const auto iterationStart = std::chrono::steady_clock::now();
		legalizer.run(function, true);
		const auto legalizeEnd = std::chrono::steady_clock::now();

		MachineCFGBuilder cfgBuilder;
		cfgBuilder.run(function);
		MachineLivenessAnalysis livenessAnalysis;
		MachineLivenessResult liveness = livenessAnalysis.run(function);
		const auto livenessEnd = std::chrono::steady_clock::now();

		IRCState state = buildIRCState(function, liveness);
		const auto buildEnd = std::chrono::steady_clock::now();
		runWorklists(state);
		const bool colored = assignColors(state);
		const auto colorEnd = std::chrono::steady_clock::now();
		if (timingEnabled) {
			const auto legalizeMs = std::chrono::duration_cast<std::chrono::milliseconds>(legalizeEnd - iterationStart).count();
			const auto livenessMs = std::chrono::duration_cast<std::chrono::milliseconds>(livenessEnd - legalizeEnd).count();
			const auto buildMs = std::chrono::duration_cast<std::chrono::milliseconds>(buildEnd - livenessEnd).count();
			const auto colorMs = std::chrono::duration_cast<std::chrono::milliseconds>(colorEnd - buildEnd).count();
			std::cerr << "[ra] " << function.name() << " iter=" << iteration
					  << " blocks=" << function.blocks().size()
					  << " insts=" << instructionCount(function)
					  << " vregs=" << maxReferencedVirtualReg(function)
					  << " nodes=" << state.classes.size()
					  << " edges=" << state.adjSet.size()
					  << " moves=" << state.moves.size()
					  << " colored=" << (colored ? "yes" : "no")
					  << " legalize=" << legalizeMs << "ms"
					  << " liveness=" << livenessMs << "ms"
					  << " build=" << buildMs << "ms"
					  << " color=" << colorMs << "ms\n";
		}
		if (debug.enabled) {
			RADebugIteration record;
			record.iteration = iteration;
			record.blocks = function.blocks().size();
			record.instructions = instructionCount(function);
			record.virtualRegs = maxReferencedVirtualReg(function);
			record.initialNodes = state.classes.size();
			record.interferenceEdges = state.adjSet.size();
			record.moveCount = state.moves.size();
			record.colored = colored;
			for (IRCNode node: state.spilledNodes) {
				if (node.isVirtual()) {
					record.spilledVRegs.push_back(node.vreg);
				}
			}
			debug.iterations.push_back(std::move(record));
		}
		if (!colored) {
			if (frameFinalized) {
				debug.failureReason = "coloring failed after frame finalization";
				writeRADebugReport(function, layout, debug, debugTag);
				return false;
			}
			const auto spilledVRegs = spillBatch(state);
			if (spilledVRegs.empty()) {
				debug.failureReason = "coloring failed without spill candidates";
				writeRADebugReport(function, layout, debug, debugTag);
				return false;
			}
			if (debug.enabled && !debug.iterations.empty()) {
				debug.iterations.back().spilledVRegs = spilledVRegs;
			}
			rewriteSpills(function, layout, spilledVRegs);
			frameFinalized = false;
			continue;
		}

		auto assignment = exportAssignment(state);
		if (!frameFinalized) {
			savedCalleeRegs = usedCalleeSavedRegs(assignment);
			debug.savedCalleeRegs = savedCalleeRegs;
			preserveCalleeSavedRegisters(function, layout, savedCalleeRegs);
			rewriteFrameSetup(function, layout);
			legalizer.run(function, false);
			frameFinalized = true;
			continue;
		}

		debug.finalAssignment = assignment;
		debug.finalCalleeRegs = usedCalleeSavedRegs(assignment);
		const auto missingCalleeRegs = missingCalleeSavedRegs(debug.finalCalleeRegs, savedCalleeRegs);
		if (!missingCalleeRegs.empty()) {
			preserveCalleeSavedRegisters(function, layout, missingCalleeRegs);
			appendUniqueRegs(savedCalleeRegs, missingCalleeRegs);
			debug.savedCalleeRegs = savedCalleeRegs;
			rewriteFrameSetup(function, layout);
			legalizer.run(function, false);
			continue;
		}
		applyAssignment(function, assignment);
		debug.verifyOk = verifyNoVirtualRegs(function);
		debug.success = debug.verifyOk;
		if (!debug.verifyOk) {
			debug.failureReason = "virtual registers remain after assignment";
		}
		writeRADebugReport(function, layout, debug, debugTag);
		return debug.verifyOk;
	}

	debug.hitIterationLimit = true;
	debug.failureReason = "hit RA iteration limit";
	writeRADebugReport(function, layout, debug, debugTag);
	return false;
}
