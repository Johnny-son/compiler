#pragma once

#include <string>
#include <unordered_map>

#include "backend/include/Asm.h"
#include "backend/include/FrameLayout.h"

class BasicBlock;

class InstructionSelector {

public:
	InstructionSelector(IRFunctionView function, const FunctionFrameLayout & layout);

	AsmFunction run();

private:
	void translateBlock(const IRBasicBlockView & block, bool isEntryBlock);
	void translateInst(const IRInstView & inst);
	void translateEntry();
	void translateAlloca(const IRInstView & inst);
	void translateLoad(const IRInstView & inst);
	void translateStore(const IRInstView & inst);
	void translateBinary(const IRInstView & inst);
	void translateICmp(const IRInstView & inst);
	void translateZExt(const IRInstView & inst);
	void translateGEP(const IRInstView & inst);
	void translateCall(const IRInstView & inst);
	void translateBranch(const IRInstView & inst);
	void translateReturn(const IRInstView & inst);

	void loadValue(const IRValueView & value, const std::string & reg);
	void storeValue(const std::string & reg, const IRValueView & value);
	void loadAddress(const IRValueView & value, const std::string & reg);
	void loadFromPointer(const IRValueView & ptr, Type * valueType, const std::string & reg);
	void storeToPointer(const std::string & reg, const IRValueView & ptr, Type * valueType);
	void loadAddressOfGlobal(const IRValueView & value, const std::string & reg);

	std::string labelName(BasicBlock * block);
	const StackSlotInfo * slotOf(const IRValueView & value) const;
	bool isEightByteType(Type * type) const;
	std::string loadOpcode(Type * type) const;
	std::string storeOpcode(Type * type) const;

private:
	IRFunctionView function;
	const FunctionFrameLayout & frameLayout;
	AsmFunction asmFunction;
	std::unordered_map<BasicBlock *, std::string> blockLabels;
	int nextLabelIndex = 0;
};
