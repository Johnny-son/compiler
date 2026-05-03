#pragma once

#include <string>
#include <unordered_map>

#include "backend/include/FrameLayout.h"
#include "backend/include/MachineIR.h"

class BasicBlock;

class InstructionSelector {

public:
	InstructionSelector(IRFunctionView function, const FunctionFrameLayout & layout);

	MachineFunction run();

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

	MachineOperand newVRegDef();
	MachineOperand loadValue(const IRValueView & value);
	void loadValueTo(const IRValueView & value, const MachineOperand & dst);
	void storeValue(const MachineOperand & src, const IRValueView & value);
	void loadAddress(const IRValueView & value, const MachineOperand & dst);
	void loadFromPointer(const IRValueView & ptr, Type * valueType, const MachineOperand & dst);
	void storeToPointer(const MachineOperand & src, const IRValueView & ptr, Type * valueType);
	void loadAddressOfGlobal(const IRValueView & value, const MachineOperand & dst);

	std::string labelName(BasicBlock * block);
	const StackSlotInfo * slotOf(const IRValueView & value) const;
	bool isEightByteType(Type * type) const;
	MachineOpcode loadOpcode(Type * type) const;
	MachineOpcode storeOpcode(Type * type) const;

private:
	IRFunctionView function;
	const FunctionFrameLayout & frameLayout;
	MachineFunction machineFunction;
	std::unordered_map<BasicBlock *, std::string> blockLabels;
	int nextLabelIndex = 0;
};
