#pragma once

#include <string>
#include <unordered_map>

#include "backend/include/Asm.h"
#include "backend/include/FrameLayout.h"

class InstructionSelector {

public:
	InstructionSelector(IRFunctionView function, const FunctionFrameLayout & layout);

	AsmFunction run();

private:
	void translateInst(const IRInstView & inst);
	void translateEntry();
	void translateExit(const IRInstView & inst);
	void translateAssign(const IRInstView & inst);
	void translateBinary(const IRInstView & inst, const std::string & opcode);
	void translateCall(const IRInstView & inst);
	void translateLabel(const IRInstView & inst);
	void translateGoto(const IRInstView & inst);

	void loadValue(const IRValueView & value, const std::string & reg);
	void storeValue(const std::string & reg, const IRValueView & value);
	void loadAddressOfGlobal(const IRValueView & value, const std::string & reg);

	std::string ensureLabelName(LabelInstruction * label);
	const StackSlotInfo * slotOf(const IRValueView & value) const;
	bool isEightByteType(Type * type) const;
	std::string loadOpcode(Type * type) const;
	std::string storeOpcode(Type * type) const;

private:
	IRFunctionView function;
	const FunctionFrameLayout & frameLayout;
	AsmFunction asmFunction;
	std::unordered_map<LabelInstruction *, std::string> labelNames;
	int nextLabelIndex = 0;
};
