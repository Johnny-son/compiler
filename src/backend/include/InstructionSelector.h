#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "FrameLayout.h"
#include "MachineIR.h"

class BasicBlock;
class BinaryInst;
class Function;
class ICmpInst;
class Value;

enum class RecognizedHelperKind {
	None,
	BitAnd,
	BitOr,
	BitXor,
	BitNot,
	ConstZero,
	Add,
	Sub,
	NegSum,
	ModMul998244353,
	SMax,
	SMin,
};

class InstructionSelector {

public:
	InstructionSelector(IRFunctionView function, const FunctionFrameLayout & layout);

	MachineFunction run();

private:
	void translateBlock(const IRBasicBlockView & block, bool isEntryBlock);
	void translateInst(const IRInstView & inst);
	void translateEntry();
	void analyzeLocalValues();
	void translateAlloca(const IRInstView & inst);
	void translateLoad(const IRInstView & inst);
	void translateStore(const IRInstView & inst);
	void translateBinary(const IRInstView & inst);
	void translateICmp(const IRInstView & inst);
	void translateFCmp(const IRInstView & inst);
	void translateZExt(const IRInstView & inst);
	void translateCast(const IRInstView & inst);
	void translateGEP(const IRInstView & inst);
	void translateCall(const IRInstView & inst);
	bool translateRecognizedHelperCall(const IRInstView & inst);
	void translatePhi(const IRInstView & inst);
	void translateBranch(const IRInstView & inst);
	void translateReturn(const IRInstView & inst);

	bool isModuloZeroBranchRemainder(BinaryInst * remainder) const;
	bool matchModuloZeroCompare(ICmpInst * cmp, BinaryInst *& remainder, int32_t & mask) const;
	bool tryEmitModuloZeroBranch(ICmpInst * cmp, const std::string & trueLabel);
	MachineOperand newVRegDef(RegisterClass regClass = RegisterClass::GPR);
	MachineOperand newVRegDef(Type * type);
	MachineOperand loadValue(const IRValueView & value);
	void loadValueTo(const IRValueView & value, const MachineOperand & dst);
	void storeValue(const MachineOperand & src, const IRValueView & value);
	[[nodiscard]] std::optional<MachineOperand> cachedValue(const IRValueView & value) const;
	bool rememberValue(Value * value, const MachineOperand & operand);
	RecognizedHelperKind classifyHelper(Function * callee);
	[[nodiscard]] bool isLocalOnlyValue(const IRValueView & value) const;
	[[nodiscard]] bool isDefinedInCurrentBlock(const IRValueView & value) const;
	void storeZeroInitializer(const IRValueView & ptr, Type * valueType);
	void loadAddress(const IRValueView & value, const MachineOperand & dst);
	void loadFromPointer(const IRValueView & ptr, Type * valueType, const MachineOperand & dst);
	void storeToPointer(const MachineOperand & src, const IRValueView & ptr, Type * valueType);
	void loadAddressOfGlobal(const IRValueView & value, const MachineOperand & dst);
	bool hasPhiCopiesForEdge(BasicBlock * successor, BasicBlock * predecessor) const;
	void emitPhiCopies(BasicBlock * successor, BasicBlock * predecessor);
	std::string edgeCopyLabel(BasicBlock * from, BasicBlock * to);

	std::string labelName(BasicBlock * block);
	const StackSlotInfo * slotOf(const IRValueView & value) const;
	bool isEightByteType(Type * type) const;
	bool isFloatType(Type * type) const;
	RegisterClass regClassForType(Type * type) const;
	MachineOpcode loadOpcode(Type * type) const;
	MachineOpcode storeOpcode(Type * type) const;

private:
	IRFunctionView function;
	const FunctionFrameLayout & frameLayout;
	MachineFunction machineFunction;
	std::unordered_map<BasicBlock *, std::string> blockLabels;
	std::unordered_map<Value *, BasicBlock *> valueBlocks;
	std::unordered_set<Value *> localOnlyValues;
	std::unordered_set<Value *> localValuesUsedAfterCall;
	std::unordered_set<Value *> crossBlockBranchCompareOperands;
	std::unordered_set<BasicBlock *> callBlocks;
	std::unordered_map<Value *, MachineOperand> localValueCache;
	std::unordered_map<std::string, MachineOperand> gepPrefixCache;
	std::unordered_map<Value *, MachineOperand> phiValueRegs;
	std::unordered_map<Function *, RecognizedHelperKind> helperKindCache;
	BasicBlock * currentIRBlock = nullptr;
	bool localValueCacheEnabled = true;
	bool localFprValueCacheEnabled = false;
	int nextLabelIndex = 0;
};
