#include "backend/include/InstructionSelector.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include "BasicBlock.h"
#include "Function.h"
#include "ir/include/Type.h"
#include "ir/Instructions/AllocaInst.h"
#include "ir/Instructions/BinaryInst.h"
#include "ir/Instructions/GetElementPtrInst.h"
#include "ir/Instructions/ICmpInst.h"
#include "ir/Types/ArrayType.h"
#include "ir/Types/PointerType.h"

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
constexpr const char * REG_T4 = "t4";

Type * pointeeType(Value * value)
{
	if (value == nullptr) {
		return nullptr;
	}

	auto * ptrType = dynamic_cast<PointerType *>(value->getType());
	if (ptrType == nullptr) {
		return value->getType();
	}
	return const_cast<Type *>(ptrType->getPointeeType());
}

std::string sanitizeLabelPart(std::string name)
{
	for (char & ch: name) {
		if (ch == '.') {
			continue;
		}
		if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
			ch = '_';
		}
	}
	return name;
}

} // namespace

InstructionSelector::InstructionSelector(IRFunctionView function, const FunctionFrameLayout & layout)
	: function(function), frameLayout(layout), asmFunction(function.name())
{}

AsmFunction InstructionSelector::run()
{
	translateEntry();

	bool firstBlock = true;
	for (const auto & block: function.blocks()) {
		translateBlock(block, firstBlock);
		firstBlock = false;
	}

	return asmFunction;
}

void InstructionSelector::translateBlock(const IRBasicBlockView & block, bool isEntryBlock)
{
	if (!block.valid()) {
		return;
	}

	if (!isEntryBlock) {
		asmFunction.emitLabel(labelName(block.raw()));
	}

	for (const auto & inst: block.instructions()) {
		translateInst(inst);
	}
}

void InstructionSelector::translateInst(const IRInstView & inst)
{
	switch (inst.kind()) {
		case IRInstKind::Alloca:
			translateAlloca(inst);
			break;
		case IRInstKind::Load:
			translateLoad(inst);
			break;
		case IRInstKind::Store:
			translateStore(inst);
			break;
		case IRInstKind::Binary:
			translateBinary(inst);
			break;
		case IRInstKind::ICmp:
			translateICmp(inst);
			break;
		case IRInstKind::ZExt:
			translateZExt(inst);
			break;
		case IRInstKind::GetElementPtr:
			translateGEP(inst);
			break;
		case IRInstKind::Call:
			translateCall(inst);
			break;
		case IRInstKind::Branch:
			translateBranch(inst);
			break;
		case IRInstKind::Return:
			translateReturn(inst);
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

	for (std::size_t index = FunctionFrameLayout::argRegCount; index < params.size(); ++index) {
		const int64_t callerArgOffset =
			static_cast<int64_t>((index - FunctionFrameLayout::argRegCount) * FunctionFrameLayout::stackSlotSize);
		asmFunction.emitOp(loadOpcode(params[index].type()), {AsmOperand::reg(REG_T0), AsmOperand::mem(REG_FP, callerArgOffset)});
		storeValue(REG_T0, params[index]);
	}
}

void InstructionSelector::translateAlloca(const IRInstView &)
{
	// alloca 的空间已经由 FrameLayout 分配在当前函数栈帧中。
}

void InstructionSelector::translateLoad(const IRInstView & inst)
{
	if (inst.operandCount() < 1 || !inst.hasResult()) {
		return;
	}

	loadFromPointer(inst.operand(0), inst.type(), REG_T0);
	storeValue(REG_T0, inst.result());
}

void InstructionSelector::translateStore(const IRInstView & inst)
{
	if (inst.operandCount() < 2) {
		return;
	}

	const IRValueView value = inst.operand(0);
	const IRValueView ptr = inst.operand(1);
	loadValue(value, REG_T0);
	storeToPointer(REG_T0, ptr, value.type());
}

void InstructionSelector::translateBinary(const IRInstView & inst)
{
	auto * binaryInst = dynamic_cast<BinaryInst *>(inst.raw());
	if (binaryInst == nullptr || inst.operandCount() < 2 || !inst.hasResult()) {
		return;
	}

	const char * opcode = "addw";
	switch (binaryInst->getBinaryOp()) {
		case BinaryInst::Op::Add:
			opcode = "addw";
			break;
		case BinaryInst::Op::Sub:
			opcode = "subw";
			break;
		case BinaryInst::Op::Mul:
			opcode = "mulw";
			break;
		case BinaryInst::Op::SDiv:
			opcode = "divw";
			break;
		case BinaryInst::Op::SRem:
			opcode = "remw";
			break;
		default:
			return;
	}

	loadValue(inst.operand(0), REG_T0);
	loadValue(inst.operand(1), REG_T1);
	asmFunction.emitOp(opcode, {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T1)});
	storeValue(REG_T2, inst.result());
}

void InstructionSelector::translateICmp(const IRInstView & inst)
{
	auto * cmpInst = dynamic_cast<ICmpInst *>(inst.raw());
	if (cmpInst == nullptr || inst.operandCount() < 2 || !inst.hasResult()) {
		return;
	}

	loadValue(inst.operand(0), REG_T0);
	loadValue(inst.operand(1), REG_T1);

	switch (cmpInst->getPredicate()) {
		case ICmpInst::Predicate::EQ:
			asmFunction.emitOp("xor", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T1)});
			asmFunction.emitOp("seqz", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T2)});
			break;
		case ICmpInst::Predicate::NE:
			asmFunction.emitOp("xor", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T1)});
			asmFunction.emitOp("snez", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T2)});
			break;
		case ICmpInst::Predicate::SLT:
			asmFunction.emitOp("slt", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T1)});
			break;
		case ICmpInst::Predicate::SGT:
			asmFunction.emitOp("slt", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T1), AsmOperand::reg(REG_T0)});
			break;
		case ICmpInst::Predicate::SLE:
			asmFunction.emitOp("slt", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T1), AsmOperand::reg(REG_T0)});
			asmFunction.emitOp("xori", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T2), AsmOperand::immValue(1)});
			break;
		case ICmpInst::Predicate::SGE:
			asmFunction.emitOp("slt", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T1)});
			asmFunction.emitOp("xori", {AsmOperand::reg(REG_T2), AsmOperand::reg(REG_T2), AsmOperand::immValue(1)});
			break;
	}

	storeValue(REG_T2, inst.result());
}

void InstructionSelector::translateZExt(const IRInstView & inst)
{
	if (inst.operandCount() < 1 || !inst.hasResult()) {
		return;
	}

	loadValue(inst.operand(0), REG_T0);
	asmFunction.emitOp("andi", {AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T0), AsmOperand::immValue(1)});
	storeValue(REG_T0, inst.result());
}

void InstructionSelector::translateGEP(const IRInstView & inst)
{
	auto * gepInst = dynamic_cast<GetElementPtrInst *>(inst.raw());
	if (gepInst == nullptr || !inst.hasResult()) {
		return;
	}

	IRValueView base(gepInst->getBasePointer());
	loadAddress(base, REG_T0);

	Type * currentType = pointeeType(base.raw());
	const auto indices = gepInst->getIndices();
	for (std::size_t indexNo = 0; indexNo < indices.size(); ++indexNo) {
		Type * scaledType = currentType;
		if (indexNo > 0) {
			if (auto * arrayType = dynamic_cast<ArrayType *>(currentType); arrayType != nullptr) {
				scaledType = arrayType->getElementType();
				currentType = scaledType;
			}
		}

		const int32_t scale = scaledType != nullptr ? scaledType->getSize() : FunctionFrameLayout::stackSlotSize;
		IRValueView indexValue(indices[indexNo]);
		if (indexValue.isConstantInt()) {
			const int32_t offset = indexValue.intValue() * scale;
			if (offset != 0) {
				asmFunction.emitOp("addi", {AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T0), AsmOperand::immValue(offset)});
			}
			continue;
		}

		loadValue(indexValue, REG_T1);
		asmFunction.emitOp("li", {AsmOperand::reg(REG_T2), AsmOperand::immValue(scale)});
		asmFunction.emitOp("mul", {AsmOperand::reg(REG_T1), AsmOperand::reg(REG_T1), AsmOperand::reg(REG_T2)});
		asmFunction.emitOp("add", {AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T0), AsmOperand::reg(REG_T1)});
	}

	storeValue(REG_T0, inst.result());
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

void InstructionSelector::translateBranch(const IRInstView & inst)
{
	if (!inst.isConditionalBranch()) {
		asmFunction.emitOp("j", {AsmOperand::label(labelName(inst.targetBlockRaw()))});
		return;
	}

	loadValue(inst.operand(0), REG_T0);
	asmFunction.emitOp("bnez", {AsmOperand::reg(REG_T0), AsmOperand::label(labelName(inst.trueBlockRaw()))});
	asmFunction.emitOp("j", {AsmOperand::label(labelName(inst.falseBlockRaw()))});
}

void InstructionSelector::translateReturn(const IRInstView & inst)
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

	if (dynamic_cast<AllocaInst *>(value.raw()) != nullptr) {
		loadAddress(value, reg);
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

void InstructionSelector::loadAddress(const IRValueView & value, const std::string & reg)
{
	if (!value.valid()) {
		asmFunction.emitOp("mv", {AsmOperand::reg(reg), AsmOperand::reg(REG_ZERO)});
		return;
	}

	if (value.isGlobalVariable()) {
		loadAddressOfGlobal(value, reg);
		return;
	}

	auto * allocaInst = dynamic_cast<AllocaInst *>(value.raw());
	if (allocaInst != nullptr) {
		const auto * slot = slotOf(value);
		if (slot != nullptr) {
			asmFunction.emitOp("addi", {AsmOperand::reg(reg), AsmOperand::reg(REG_FP), AsmOperand::immValue(slot->offset)});
		}
		return;
	}

	loadValue(value, reg);
}

void InstructionSelector::loadFromPointer(const IRValueView & ptr, Type * valueType, const std::string & reg)
{
	if (dynamic_cast<AllocaInst *>(ptr.raw()) != nullptr) {
		const auto * slot = slotOf(ptr);
		if (slot != nullptr) {
			asmFunction.emitOp(loadOpcode(valueType), {AsmOperand::reg(reg), AsmOperand::mem(REG_FP, slot->offset)});
		}
		return;
	}

	if (ptr.isGlobalVariable()) {
		loadAddressOfGlobal(ptr, REG_T3);
		asmFunction.emitOp(loadOpcode(valueType), {AsmOperand::reg(reg), AsmOperand::mem(REG_T3, 0)});
		return;
	}

	loadValue(ptr, REG_T3);
	asmFunction.emitOp(loadOpcode(valueType), {AsmOperand::reg(reg), AsmOperand::mem(REG_T3, 0)});
}

void InstructionSelector::storeToPointer(const std::string & reg, const IRValueView & ptr, Type * valueType)
{
	if (dynamic_cast<AllocaInst *>(ptr.raw()) != nullptr) {
		const auto * slot = slotOf(ptr);
		if (slot != nullptr) {
			asmFunction.emitOp(storeOpcode(valueType), {AsmOperand::reg(reg), AsmOperand::mem(REG_FP, slot->offset)});
		}
		return;
	}

	if (ptr.isGlobalVariable()) {
		loadAddressOfGlobal(ptr, REG_T3);
		asmFunction.emitOp(storeOpcode(valueType), {AsmOperand::reg(reg), AsmOperand::mem(REG_T3, 0)});
		return;
	}

	loadValue(ptr, REG_T3);
	asmFunction.emitOp(storeOpcode(valueType), {AsmOperand::reg(reg), AsmOperand::mem(REG_T3, 0)});
}

void InstructionSelector::loadAddressOfGlobal(const IRValueView & value, const std::string & reg)
{
	asmFunction.emitOp("la", {AsmOperand::reg(reg), AsmOperand::symbol(value.name())});
}

std::string InstructionSelector::labelName(BasicBlock * block)
{
	if (block == nullptr) {
		return ".L_invalid";
	}

	if (block == function.raw()->getEntryBlock()) {
		return function.name();
	}

	auto iter = blockLabels.find(block);
	if (iter != blockLabels.end()) {
		return iter->second;
	}

	std::string name = ".L_" + sanitizeLabelPart(function.name()) + "_" + sanitizeLabelPart(block->getIRName());
	if (name == ".L_" + sanitizeLabelPart(function.name()) + "_") {
		name += std::to_string(nextLabelIndex++);
	}

	blockLabels.insert({block, name});
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
