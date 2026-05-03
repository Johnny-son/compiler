#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

class Value;

using MachineBlockIndex = std::size_t;

enum class RegisterClass : std::int8_t {
	GPR
};

enum class PhysicalReg : std::int16_t {
	Invalid,
	Zero,
	RA,
	SP,
	FP,
	A0,
	A1,
	A2,
	A3,
	A4,
	A5,
	A6,
	A7,
	T0,
	T1,
	T2,
	T3,
	T4,
	T5,
	T6
};

enum class MachineOpcode : std::int16_t {
	ADDI,
	ADD,
	ADDW,
	SUBW,
	MUL,
	MULW,
	DIVW,
	REMW,
	XOR,
	SLT,
	XORI,
	ANDI,
	SEQZ,
	SNEZ,
	LI,
	LA,
	LA_STACK,
	LW,
	LD,
	SW,
	SD,
	COPY,
	CALL,
	J,
	BNEZ,
	RET
};

enum class MachineOperandKind : std::int8_t {
	VirtualReg,
	PhysicalReg,
	Immediate,
	StackSlot,
	Memory,
	BlockLabel,
	GlobalSymbol,
	FunctionSymbol
};

enum class MachineOperandRole : std::int8_t {
	None,
	Use,
	Def
};

struct MachineOperand {
	MachineOperandKind kind = MachineOperandKind::Immediate;
	MachineOperandRole role = MachineOperandRole::None;
	RegisterClass regClass = RegisterClass::GPR;
	int32_t vreg = -1;
	PhysicalReg preg = PhysicalReg::Invalid;
	int64_t imm = 0;
	Value * stackValue = nullptr;
	bool memoryBaseIsPhysical = true;
	PhysicalReg memoryBasePreg = PhysicalReg::Invalid;
	int32_t memoryBaseVReg = -1;
	int64_t memoryOffset = 0;
	std::string text;

	static MachineOperand vregUse(int32_t id, RegisterClass regClass = RegisterClass::GPR);
	static MachineOperand vregDef(int32_t id, RegisterClass regClass = RegisterClass::GPR);
	static MachineOperand pregUse(PhysicalReg reg);
	static MachineOperand pregDef(PhysicalReg reg);
	static MachineOperand immValue(int64_t value);
	static MachineOperand stackSlot(Value * value);
	static MachineOperand mem(PhysicalReg base, int64_t offset);
	static MachineOperand memVReg(int32_t base, int64_t offset);
	static MachineOperand blockLabel(std::string label);
	static MachineOperand globalSymbol(std::string symbol);
	static MachineOperand functionSymbol(std::string symbol);

	[[nodiscard]] bool isReg() const;
	[[nodiscard]] bool isVirtualReg() const;
	[[nodiscard]] bool isPhysicalReg() const;
	[[nodiscard]] MachineOperand asUse() const;
	[[nodiscard]] MachineOperand asDef() const;
};

struct MachineInstr {
	MachineOpcode opcode;
	std::vector<MachineOperand> operands;
	std::vector<PhysicalReg> implicitUses;
	std::vector<PhysicalReg> implicitDefs;
	std::string comment;

	static MachineInstr make(MachineOpcode opcode, std::vector<MachineOperand> operands = {});
};

class MachineBasicBlock {

public:
	explicit MachineBasicBlock(std::string label = "");

	[[nodiscard]] const std::string & label() const;
	[[nodiscard]] const std::vector<MachineInstr> & instructions() const;
	[[nodiscard]] std::vector<MachineInstr> & instructions();
	[[nodiscard]] const std::vector<MachineBlockIndex> & successors() const;
	[[nodiscard]] const std::vector<MachineBlockIndex> & predecessors() const;

	void emit(const MachineInstr & inst);
	void emit(MachineOpcode opcode, std::vector<MachineOperand> operands = {});
	void addSuccessor(MachineBlockIndex index);
	void addPredecessor(MachineBlockIndex index);
	void clearCFGLinks();

private:
	std::string blockLabel;
	std::vector<MachineInstr> insts;
	std::vector<MachineBlockIndex> succs;
	std::vector<MachineBlockIndex> preds;
};

class MachineFunction {

public:
	explicit MachineFunction(std::string name = "");

	[[nodiscard]] const std::string & name() const;
	[[nodiscard]] const std::vector<MachineBasicBlock> & blocks() const;
	[[nodiscard]] std::vector<MachineBasicBlock> & blocks();

	MachineBasicBlock & createBlock(const std::string & label);
	[[nodiscard]] MachineBasicBlock * currentBlock();
	[[nodiscard]] const MachineBasicBlock * currentBlock() const;
	void setCurrentBlock(std::size_t index);
	void emit(MachineOpcode opcode, std::vector<MachineOperand> operands = {});
	int32_t createVirtualReg(RegisterClass regClass = RegisterClass::GPR);

private:
	std::string funcName;
	std::vector<MachineBasicBlock> basicBlocks;
	std::size_t currentBlockIndex = 0;
	int32_t nextVReg = 0;
};

class TargetRegisterInfo {

public:
	static const char * name(PhysicalReg reg);
	static bool isValid(PhysicalReg reg);
};

class MachineDebugPrinter {

public:
	static std::string toString(const MachineFunction & function);
};
