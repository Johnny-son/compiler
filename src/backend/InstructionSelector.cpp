#include "backend/include/InstructionSelector.h"

#include <utility>

#include "ir/include/Type.h"
#include "ir/Instructions/LabelInstruction.h"

namespace {

constexpr const char * REG_ZERO = "zero";
constexpr const char * REG_RA = "ra";
constexpr const char * REG_SP = "sp";
constexpr const char * REG_FP = "fp";
constexpr const char * REG_A[] = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};
constexpr const char * REG_T0 = "t0";
constexpr const char * REG_T1 = "t1";
constexpr const char * REG_T2 = "t2";
constexpr const char * REG_T3 = "t3";

} // namespace

InstructionSelector::InstructionSelector(IRFunctionView function, const FunctionFrameLayout & layout)
	: function(function), frameLayout(layout), asmFunction(function.name())
{}

AsmFunction InstructionSelector::run()
{
	for (const auto & inst: function.instructions()) {
		translateInst(inst);
	}

	return asmFunction;
}

void InstructionSelector::translateInst(const IRInstView & inst)
{
	switch (inst.kind()) {
		case IRInstKind::Entry:
			translateEntry();
			break;
		case IRInstKind::Exit:
			translateExit(inst);
			break;
		case IRInstKind::Assign:
			translateAssign(inst);
			break;
		case IRInstKind::Add:
			translateBinary(inst, "addw");
			break;
		case IRInstKind::Sub:
			translateBinary(inst, "subw");
			break;
		case IRInstKind::Mul:
			translateBinary(inst, "mulw");
			break;
		case IRInstKind::Div:
			translateBinary(inst, "divw");
			break;
		case IRInstKind::Mod:
			translateBinary(inst, "remw");
			break;
		case IRInstKind::FuncCall:
			translateCall(inst);
			break;
		case IRInstKind::Label:
			translateLabel(inst);
			break;
		case IRInstKind::Goto:
			translateGoto(inst);
			break;
		default:
			break;
	}
}

void InstructionSelector::translateEntry()
{
	const int frameSize = frameLayout.frameSize();
	if (frameSize > 0) {
		asmFunction.emitOp("addi", {AsmOperand::reg(REG_SP), AsmOperand::reg(REG_SP), AsmOperand::immValue(-frameSize)});
	}

	asmFunction.emitOp(
		"sd",
		{AsmOperand::reg(REG_RA), AsmOperand::mem(REG_SP, frameSize + FunctionFrameLayout::savedRaOffset)});
	asmFunction.emitOp(
		"sd",
		{AsmOperand::reg(REG_FP), AsmOperand::mem(REG_SP, frameSize + FunctionFrameLayout::savedFpOffset)});
	asmFunction.emitOp("addi", {AsmOperand::reg(REG_FP), AsmOperand::reg(REG_SP), AsmOperand::immValue(frameSize)});

	const auto params = function.params();
	const int maxParamRegs = std::min(static_cast<int>(params.size()), FunctionFrameLayout::argRegCount);
	for (int index = 0; index < maxParamRegs; ++index) {
		storeValue(REG_A[index], params[index]);
	}
}

void InstructionSelector::translateExit(const IRInstView & inst)
{
	if (inst.operandCount() > 0) {
		loadValue(inst.operand(0), REG_A[0]);
	}

	asmFunction.emitOp("ld", {AsmOperand::reg(REG_RA), AsmOperand::mem(REG_FP, FunctionFrameLayout::savedRaOffset)});
	asmFunction.emitOp("ld", {AsmOperand::reg(REG_T0), AsmOperand::mem(REG_FP, FunctionFrameLayout::savedFpOffset)});
	asmFunction.emitOp("mv", {AsmOperand::reg(REG_SP), AsmOperand::reg(REG_FP)});
	asmFunction.emitOp("mv", {AsmOperand::reg(REG_FP), AsmOperand::reg(REG_T0)});
	asmFunction.emitOp("ret");
}

void InstructionSelector::translateAssign(const IRInstView & inst)
{
	if (inst.operandCount() < 2) {
		return;
	}

	const IRValueView dst = inst.operand(0);
	const IRValueView src = inst.operand(1);
	loadValue(src, REG_T0);
	storeValue(REG_T0, dst);
}

void InstructionSelector::translateBinary(const IRInstView & inst, const std::string & opcode)
{
	if (inst.operandCount() < 2 || !inst.hasResult()) {
		return;
	}

	loadValue(inst.operand(0), REG_T0);
	loadValue(inst.operand(1), REG_T1);
	asmFunction.emitOp(opcode, {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T1)});
	storeValue(REG_T2, inst.result());
}

void InstructionSelector::translateCall(const IRInstView & inst)
{
	const std::size_t argCount = inst.operandCount();
	const std::size_t regArgCount = std::min<std::size_t>(argCount, FunctionFrameLayout::argRegCount);

	for (std::size_t index = 0; index < regArgCount; ++index) {
		loadValue(inst.operand(index), REG_A[index]);
	}

	for (std::size_t index = regArgCount; index < argCount; ++index) {
		loadValue(inst.operand(index), REG_T0);
		asmFunction.emitOp(
			"sd",
			{AsmOperand::reg(REG_T0),
			 AsmOperand::mem(REG_SP, static_cast<int64_t>((index - regArgCount) * FunctionFrameLayout::stackSlotSize))});
	}

	asmFunction.emitOp("call", {AsmOperand::symbol(inst.calledFunctionName())});

	if (inst.hasResult()) {
		storeValue(REG_A[0], inst.result());
	}
}

void InstructionSelector::translateLabel(const IRInstView & inst)
{
	auto * rawLabel = static_cast<LabelInstruction *>(inst.raw());
	asmFunction.emitLabel(ensureLabelName(rawLabel));
}

void InstructionSelector::translateGoto(const IRInstView & inst)
{
	asmFunction.emitOp("j", {AsmOperand::label(ensureLabelName(inst.targetLabelRaw()))});
}

void InstructionSelector::loadValue(const IRValueView & value, const std::string & reg)
{
	if (!value.valid()) {
		asmFunction.emitOp("mv", {AsmOperand::reg(reg), AsmOperand::reg(REG_ZERO)});
		return;
	}

	if (value.isConstantInt()) {
		asmFunction.emitOp("li", {AsmOperand::reg(reg), AsmOperand::immValue(value.intValue())});
		return;
	}

	if (value.isGlobalVariable()) {
		loadAddressOfGlobal(value, REG_T3);
		asmFunction.emitOp(loadOpcode(value.type()), {AsmOperand::reg(reg), AsmOperand::mem(REG_T3, 0)});
		return;
	}

	const auto * slot = slotOf(value);
	if (slot != nullptr) {
		asmFunction.emitOp(loadOpcode(value.type()), {AsmOperand::reg(reg), AsmOperand::mem(REG_FP, slot->offset)});
	}
}

void InstructionSelector::storeValue(const std::string & reg, const IRValueView & value)
{
	if (!value.valid()) {
		return;
	}

	if (value.isGlobalVariable()) {
		loadAddressOfGlobal(value, REG_T3);
		asmFunction.emitOp(storeOpcode(value.type()), {AsmOperand::reg(reg), AsmOperand::mem(REG_T3, 0)});
		return;
	}

	const auto * slot = slotOf(value);
	if (slot != nullptr) {
		asmFunction.emitOp(storeOpcode(value.type()), {AsmOperand::reg(reg), AsmOperand::mem(REG_FP, slot->offset)});
	}
}

void InstructionSelector::loadAddressOfGlobal(const IRValueView & value, const std::string & reg)
{
	asmFunction.emitOp("la", {AsmOperand::reg(reg), AsmOperand::symbol(value.name())});
}

std::string InstructionSelector::ensureLabelName(LabelInstruction * label)
{
	if (label == nullptr) {
		return ".L_invalid";
	}

	auto iter = labelNames.find(label);
	if (iter != labelNames.end()) {
		return iter->second;
	}

	std::string name = label->getIRName();
	if (name.empty()) {
		name = ".L_" + function.name() + "_" + std::to_string(nextLabelIndex++);
	}

	labelNames.insert({label, name});
	return name;
}

const StackSlotInfo * InstructionSelector::slotOf(const IRValueView & value) const
{
	return frameLayout.slotOf(value.raw());
}

bool InstructionSelector::isEightByteType(Type * type) const
{
	return type != nullptr && (type->isPointerType() || type->getSize() > 4);
}

std::string InstructionSelector::loadOpcode(Type * type) const
{
	return isEightByteType(type) ? "ld" : "lw";
}

std::string InstructionSelector::storeOpcode(Type * type) const
{
	return isEightByteType(type) ? "sd" : "sw";
}
