#pragma once

#include <cstdio>
#include <string>
#include <vector>

enum class AsmOperandKind : std::int8_t {
	None,
	Register,
	Immediate,
	Memory,
	Label,
	Symbol
};

struct AsmOperand {
	AsmOperandKind kind = AsmOperandKind::None;
	std::string text;
	int64_t imm = 0;
	std::string base;
	int64_t offset = 0;

	static AsmOperand reg(const std::string & name);
	static AsmOperand immValue(int64_t value);
	static AsmOperand mem(const std::string & baseReg, int64_t disp);
	static AsmOperand label(const std::string & name);
	static AsmOperand symbol(const std::string & name);

	[[nodiscard]] std::string toString() const;
};

struct AsmInstruction {
	bool isLabel = false;
	std::string opcode;
	std::vector<AsmOperand> operands;
	std::string comment;

	static AsmInstruction makeLabel(const std::string & name);
	static AsmInstruction makeOp(const std::string & opcode, std::vector<AsmOperand> operands = {});
};

class AsmFunction {

public:
	explicit AsmFunction(std::string name = "");

	[[nodiscard]] const std::string & name() const;
	[[nodiscard]] const std::vector<AsmInstruction> & instructions() const;

	void emit(const AsmInstruction & inst);
	void emitLabel(const std::string & label);
	void emitOp(const std::string & opcode, std::vector<AsmOperand> operands = {});

private:
	std::string funcName;
	std::vector<AsmInstruction> insts;
};

class AsmPrinter {

public:
	static void printFunction(FILE * fp, const AsmFunction & function);
	static std::string toString(const AsmFunction & function);
};
