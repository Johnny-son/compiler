#include "backend/include/MachineIR.h"

#include "ir/include/Value.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace {

const char * opcodeName(MachineOpcode opcode)
{
	switch (opcode) {
		case MachineOpcode::ADDI:
			return "addi";
		case MachineOpcode::ADD:
			return "add";
		case MachineOpcode::ADDW:
			return "addw";
		case MachineOpcode::SUBW:
			return "subw";
		case MachineOpcode::MUL:
			return "mul";
		case MachineOpcode::MULW:
			return "mulw";
		case MachineOpcode::DIVW:
			return "divw";
		case MachineOpcode::REMW:
			return "remw";
		case MachineOpcode::XOR:
			return "xor";
		case MachineOpcode::SLT:
			return "slt";
		case MachineOpcode::XORI:
			return "xori";
		case MachineOpcode::ANDI:
			return "andi";
		case MachineOpcode::SEQZ:
			return "seqz";
		case MachineOpcode::SNEZ:
			return "snez";
		case MachineOpcode::LI:
			return "li";
		case MachineOpcode::LA:
			return "la";
		case MachineOpcode::LA_STACK:
			return "la_stack";
		case MachineOpcode::LW:
			return "lw";
		case MachineOpcode::LD:
			return "ld";
		case MachineOpcode::SW:
			return "sw";
		case MachineOpcode::SD:
			return "sd";
		case MachineOpcode::COPY:
			return "copy";
		case MachineOpcode::CALL:
			return "call";
		case MachineOpcode::J:
			return "j";
		case MachineOpcode::BNEZ:
			return "bnez";
		case MachineOpcode::RET:
			return "ret";
	}
	return "unknown";
}

std::string operandToString(const MachineOperand & operand)
{
	std::string prefix;
	if (operand.role == MachineOperandRole::Def) {
		prefix = "def ";
	} else if (operand.role == MachineOperandRole::Use) {
		prefix = "use ";
	}

	switch (operand.kind) {
		case MachineOperandKind::VirtualReg:
			return prefix + "%v" + std::to_string(operand.vreg) + ":gpr";
		case MachineOperandKind::PhysicalReg:
			return prefix + TargetRegisterInfo::name(operand.preg);
		case MachineOperandKind::Immediate:
			return std::to_string(operand.imm);
		case MachineOperandKind::StackSlot:
			return "stack(" + (operand.stackValue != nullptr ? operand.stackValue->getIRName() : std::string("<fixed>")) + ")";
		case MachineOperandKind::Memory:
			if (operand.memoryBaseIsPhysical) {
				return std::to_string(operand.memoryOffset) + "(" + TargetRegisterInfo::name(operand.memoryBasePreg) + ")";
			}
			return std::to_string(operand.memoryOffset) + "(%v" + std::to_string(operand.memoryBaseVReg) + ")";
		case MachineOperandKind::BlockLabel:
		case MachineOperandKind::GlobalSymbol:
		case MachineOperandKind::FunctionSymbol:
			return operand.text;
	}
	return "";
}

} // namespace

MachineOperand MachineOperand::vregUse(int32_t id, RegisterClass regClass)
{
	MachineOperand operand;
	operand.kind = MachineOperandKind::VirtualReg;
	operand.role = MachineOperandRole::Use;
	operand.regClass = regClass;
	operand.vreg = id;
	return operand;
}

MachineOperand MachineOperand::vregDef(int32_t id, RegisterClass regClass)
{
	MachineOperand operand = vregUse(id, regClass);
	operand.role = MachineOperandRole::Def;
	return operand;
}

MachineOperand MachineOperand::pregUse(PhysicalReg reg)
{
	MachineOperand operand;
	operand.kind = MachineOperandKind::PhysicalReg;
	operand.role = MachineOperandRole::Use;
	operand.preg = reg;
	return operand;
}

MachineOperand MachineOperand::pregDef(PhysicalReg reg)
{
	MachineOperand operand = pregUse(reg);
	operand.role = MachineOperandRole::Def;
	return operand;
}

MachineOperand MachineOperand::immValue(int64_t value)
{
	MachineOperand operand;
	operand.kind = MachineOperandKind::Immediate;
	operand.imm = value;
	return operand;
}

MachineOperand MachineOperand::stackSlot(Value * value)
{
	MachineOperand operand;
	operand.kind = MachineOperandKind::StackSlot;
	operand.stackValue = value;
	return operand;
}

MachineOperand MachineOperand::mem(PhysicalReg base, int64_t offset)
{
	MachineOperand operand;
	operand.kind = MachineOperandKind::Memory;
	operand.memoryBaseIsPhysical = true;
	operand.memoryBasePreg = base;
	operand.memoryOffset = offset;
	return operand;
}

MachineOperand MachineOperand::memVReg(int32_t base, int64_t offset)
{
	MachineOperand operand;
	operand.kind = MachineOperandKind::Memory;
	operand.memoryBaseIsPhysical = false;
	operand.memoryBaseVReg = base;
	operand.memoryOffset = offset;
	return operand;
}

MachineOperand MachineOperand::blockLabel(std::string label)
{
	MachineOperand operand;
	operand.kind = MachineOperandKind::BlockLabel;
	operand.text = std::move(label);
	return operand;
}

MachineOperand MachineOperand::globalSymbol(std::string symbol)
{
	MachineOperand operand;
	operand.kind = MachineOperandKind::GlobalSymbol;
	operand.text = std::move(symbol);
	return operand;
}

MachineOperand MachineOperand::functionSymbol(std::string symbol)
{
	MachineOperand operand;
	operand.kind = MachineOperandKind::FunctionSymbol;
	operand.text = std::move(symbol);
	return operand;
}

bool MachineOperand::isReg() const
{
	return isVirtualReg() || isPhysicalReg();
}

bool MachineOperand::isVirtualReg() const
{
	return kind == MachineOperandKind::VirtualReg;
}

bool MachineOperand::isPhysicalReg() const
{
	return kind == MachineOperandKind::PhysicalReg;
}

MachineOperand MachineOperand::asUse() const
{
	MachineOperand operand = *this;
	if (operand.isReg()) {
		operand.role = MachineOperandRole::Use;
	}
	return operand;
}

MachineOperand MachineOperand::asDef() const
{
	MachineOperand operand = *this;
	if (operand.isReg()) {
		operand.role = MachineOperandRole::Def;
	}
	return operand;
}

MachineInstr MachineInstr::make(MachineOpcode opcode, std::vector<MachineOperand> operands)
{
	MachineInstr inst{opcode, std::move(operands)};
	return inst;
}

MachineBasicBlock::MachineBasicBlock(std::string label) : blockLabel(std::move(label))
{}

const std::string & MachineBasicBlock::label() const
{
	return blockLabel;
}

const std::vector<MachineInstr> & MachineBasicBlock::instructions() const
{
	return insts;
}

std::vector<MachineInstr> & MachineBasicBlock::instructions()
{
	return insts;
}

const std::vector<MachineBlockIndex> & MachineBasicBlock::successors() const
{
	return succs;
}

const std::vector<MachineBlockIndex> & MachineBasicBlock::predecessors() const
{
	return preds;
}

void MachineBasicBlock::emit(const MachineInstr & inst)
{
	insts.push_back(inst);
}

void MachineBasicBlock::emit(MachineOpcode opcode, std::vector<MachineOperand> operands)
{
	emit(MachineInstr::make(opcode, std::move(operands)));
}

void MachineBasicBlock::addSuccessor(MachineBlockIndex index)
{
	if (std::find(succs.begin(), succs.end(), index) == succs.end()) {
		succs.push_back(index);
	}
}

void MachineBasicBlock::addPredecessor(MachineBlockIndex index)
{
	if (std::find(preds.begin(), preds.end(), index) == preds.end()) {
		preds.push_back(index);
	}
}

void MachineBasicBlock::clearCFGLinks()
{
	succs.clear();
	preds.clear();
}

MachineFunction::MachineFunction(std::string name) : funcName(std::move(name))
{}

const std::string & MachineFunction::name() const
{
	return funcName;
}

const std::vector<MachineBasicBlock> & MachineFunction::blocks() const
{
	return basicBlocks;
}

std::vector<MachineBasicBlock> & MachineFunction::blocks()
{
	return basicBlocks;
}

MachineBasicBlock & MachineFunction::createBlock(const std::string & label)
{
	basicBlocks.emplace_back(label);
	currentBlockIndex = basicBlocks.size() - 1;
	return basicBlocks.back();
}

MachineBasicBlock * MachineFunction::currentBlock()
{
	if (basicBlocks.empty() || currentBlockIndex >= basicBlocks.size()) {
		return nullptr;
	}
	return &basicBlocks[currentBlockIndex];
}

const MachineBasicBlock * MachineFunction::currentBlock() const
{
	if (basicBlocks.empty() || currentBlockIndex >= basicBlocks.size()) {
		return nullptr;
	}
	return &basicBlocks[currentBlockIndex];
}

void MachineFunction::setCurrentBlock(std::size_t index)
{
	if (index < basicBlocks.size()) {
		currentBlockIndex = index;
	}
}

void MachineFunction::emit(MachineOpcode opcode, std::vector<MachineOperand> operands)
{
	if (currentBlock() == nullptr) {
		createBlock(funcName);
	}
	currentBlock()->emit(opcode, std::move(operands));
}

int32_t MachineFunction::createVirtualReg(RegisterClass)
{
	return nextVReg++;
}

const char * TargetRegisterInfo::name(PhysicalReg reg)
{
	switch (reg) {
		case PhysicalReg::Zero:
			return "zero";
		case PhysicalReg::RA:
			return "ra";
		case PhysicalReg::SP:
			return "sp";
		case PhysicalReg::FP:
			return "fp";
		case PhysicalReg::A0:
			return "a0";
		case PhysicalReg::A1:
			return "a1";
		case PhysicalReg::A2:
			return "a2";
		case PhysicalReg::A3:
			return "a3";
		case PhysicalReg::A4:
			return "a4";
		case PhysicalReg::A5:
			return "a5";
		case PhysicalReg::A6:
			return "a6";
		case PhysicalReg::A7:
			return "a7";
		case PhysicalReg::T0:
			return "t0";
		case PhysicalReg::T1:
			return "t1";
		case PhysicalReg::T2:
			return "t2";
		case PhysicalReg::T3:
			return "t3";
		case PhysicalReg::T4:
			return "t4";
		case PhysicalReg::T5:
			return "t5";
		case PhysicalReg::T6:
			return "t6";
		default:
			return "<invalid>";
	}
}

bool TargetRegisterInfo::isValid(PhysicalReg reg)
{
	return reg != PhysicalReg::Invalid;
}

std::string MachineDebugPrinter::toString(const MachineFunction & function)
{
	std::ostringstream out;
	out << "machine function " << function.name() << "\n";
	for (const auto & block: function.blocks()) {
		out << block.label() << ":\n";
		if (!block.predecessors().empty() || !block.successors().empty()) {
			out << "  # preds:";
			for (MachineBlockIndex pred: block.predecessors()) {
				out << " " << function.blocks()[pred].label();
			}
			out << "\n";

			out << "  # succs:";
			for (MachineBlockIndex succ: block.successors()) {
				out << " " << function.blocks()[succ].label();
			}
			out << "\n";
		}
		for (const auto & inst: block.instructions()) {
			out << "  " << opcodeName(inst.opcode);
			if (!inst.operands.empty()) {
				out << " ";
			}
			for (std::size_t index = 0; index < inst.operands.size(); ++index) {
				if (index != 0) {
					out << ", ";
				}
				out << operandToString(inst.operands[index]);
			}
			if (!inst.comment.empty()) {
				out << "  # " << inst.comment;
			}
			out << "\n";
		}
	}
	return out.str();
}
