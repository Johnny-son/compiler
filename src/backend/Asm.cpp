#include "backend/include/Asm.h"

#include <sstream>

AsmOperand AsmOperand::reg(const std::string & name)
{
	AsmOperand operand;
	operand.kind = AsmOperandKind::Register;
	operand.text = name;
	return operand;
}

AsmOperand AsmOperand::immValue(int64_t value)
{
	AsmOperand operand;
	operand.kind = AsmOperandKind::Immediate;
	operand.imm = value;
	return operand;
}

AsmOperand AsmOperand::mem(const std::string & baseReg, int64_t disp)
{
	AsmOperand operand;
	operand.kind = AsmOperandKind::Memory;
	operand.base = baseReg;
	operand.offset = disp;
	return operand;
}

AsmOperand AsmOperand::label(const std::string & name)
{
	AsmOperand operand;
	operand.kind = AsmOperandKind::Label;
	operand.text = name;
	return operand;
}

AsmOperand AsmOperand::symbol(const std::string & name)
{
	AsmOperand operand;
	operand.kind = AsmOperandKind::Symbol;
	operand.text = name;
	return operand;
}

std::string AsmOperand::toString() const
{
	switch (kind) {
		case AsmOperandKind::Register:
		case AsmOperandKind::Label:
		case AsmOperandKind::Symbol:
			return text;
		case AsmOperandKind::Immediate:
			return std::to_string(imm);
		case AsmOperandKind::Memory:
			return std::to_string(offset) + "(" + base + ")";
		default:
			return "";
	}
}

AsmInstruction AsmInstruction::makeLabel(const std::string & name)
{
	AsmInstruction inst;
	inst.isLabel = true;
	inst.opcode = name;
	return inst;
}

AsmInstruction AsmInstruction::makeOp(const std::string & opcode, std::vector<AsmOperand> operands)
{
	AsmInstruction inst;
	inst.opcode = opcode;
	inst.operands = std::move(operands);
	return inst;
}

AsmFunction::AsmFunction(std::string name) : funcName(std::move(name))
{}

const std::string & AsmFunction::name() const
{
	return funcName;
}

const std::vector<AsmInstruction> & AsmFunction::instructions() const
{
	return insts;
}

void AsmFunction::emit(const AsmInstruction & inst)
{
	insts.push_back(inst);
}

void AsmFunction::emitLabel(const std::string & label)
{
	emit(AsmInstruction::makeLabel(label));
}

void AsmFunction::emitOp(const std::string & opcode, std::vector<AsmOperand> operands)
{
	emit(AsmInstruction::makeOp(opcode, std::move(operands)));
}

void AsmPrinter::printFunction(FILE * fp, const AsmFunction & function)
{
	if (fp == nullptr) {
		return;
	}

	fprintf(fp, ".text\n");
	fprintf(fp, ".globl %s\n", function.name().c_str());
	fprintf(fp, ".type %s, @function\n", function.name().c_str());
	fprintf(fp, "%s:\n", function.name().c_str());

	for (const auto & inst: function.instructions()) {
		if (inst.isLabel) {
			fprintf(fp, "%s:\n", inst.opcode.c_str());
			continue;
		}

		std::string line = "  " + inst.opcode;
		if (!inst.operands.empty()) {
			line += " ";
		}

		for (std::size_t index = 0; index < inst.operands.size(); ++index) {
			if (index != 0) {
				line += ", ";
			}
			line += inst.operands[index].toString();
		}

		if (!inst.comment.empty()) {
			line += "  # " + inst.comment;
		}

		fprintf(fp, "%s\n", line.c_str());
	}

	fprintf(fp, ".size %s, .-%s\n", function.name().c_str(), function.name().c_str());
}

std::string AsmPrinter::toString(const AsmFunction & function)
{
	std::ostringstream out;
	out << ".text\n";
	out << ".globl " << function.name() << "\n";
	out << ".type " << function.name() << ", @function\n";
	out << function.name() << ":\n";

	for (const auto & inst: function.instructions()) {
		if (inst.isLabel) {
			out << inst.opcode << ":\n";
			continue;
		}

		out << "  " << inst.opcode;
		if (!inst.operands.empty()) {
			out << " ";
		}

		for (std::size_t index = 0; index < inst.operands.size(); ++index) {
			if (index != 0) {
				out << ", ";
			}
			out << inst.operands[index].toString();
		}

		if (!inst.comment.empty()) {
			out << "  # " << inst.comment;
		}

		out << "\n";
	}

	out << ".size " << function.name() << ", .-" << function.name() << "\n";
	return out.str();
}
