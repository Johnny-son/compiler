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

constexpr PhysicalReg REG_A[] = {
	PhysicalReg::A0,
	PhysicalReg::A1,
	PhysicalReg::A2,
	PhysicalReg::A3,
	PhysicalReg::A4,
	PhysicalReg::A5,
	PhysicalReg::A6,
	PhysicalReg::A7,
};

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
	: function(function), frameLayout(layout), machineFunction(function.name())
{}

MachineFunction InstructionSelector::run()
{
	machineFunction.createBlock(function.name());
	translateEntry();

	bool firstBlock = true;
	for (const auto & block: function.blocks()) {
		translateBlock(block, firstBlock);
		firstBlock = false;
	}

	return machineFunction;
}

void InstructionSelector::translateBlock(const IRBasicBlockView & block, bool isEntryBlock)
{
	if (!block.valid()) {
		return;
	}

	if (!isEntryBlock) {
		machineFunction.createBlock(labelName(block.raw()));
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
		machineFunction.emit(
			MachineOpcode::ADDI,
			{MachineOperand::pregDef(PhysicalReg::SP),
			 MachineOperand::pregUse(PhysicalReg::SP),
			 MachineOperand::immValue(-frameSize)});
	}

	machineFunction.emit(
		MachineOpcode::SD,
		{MachineOperand::pregUse(PhysicalReg::RA),
		 MachineOperand::mem(PhysicalReg::SP, frameSize + FunctionFrameLayout::savedRaOffset)});
	machineFunction.emit(
		MachineOpcode::SD,
		{MachineOperand::pregUse(PhysicalReg::FP),
		 MachineOperand::mem(PhysicalReg::SP, frameSize + FunctionFrameLayout::savedFpOffset)});
	machineFunction.emit(
		MachineOpcode::ADDI,
		{MachineOperand::pregDef(PhysicalReg::FP),
		 MachineOperand::pregUse(PhysicalReg::SP),
		 MachineOperand::immValue(frameSize)});

	const auto params = function.params();
	const int maxParamRegs = std::min(static_cast<int>(params.size()), FunctionFrameLayout::argRegCount);
	for (int index = 0; index < maxParamRegs; ++index) {
		storeValue(MachineOperand::pregUse(REG_A[index]), params[index]);
	}

	for (std::size_t index = FunctionFrameLayout::argRegCount; index < params.size(); ++index) {
		const int64_t callerArgOffset =
			static_cast<int64_t>((index - FunctionFrameLayout::argRegCount) * FunctionFrameLayout::stackSlotSize);
		auto tmp = newVRegDef();
		machineFunction.emit(
			loadOpcode(params[index].type()),
			{tmp, MachineOperand::mem(PhysicalReg::FP, callerArgOffset)});
		storeValue(tmp.asUse(), params[index]);
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

	auto tmp = newVRegDef();
	loadFromPointer(inst.operand(0), inst.type(), tmp);
	storeValue(tmp.asUse(), inst.result());
}

void InstructionSelector::translateStore(const IRInstView & inst)
{
	if (inst.operandCount() < 2) {
		return;
	}

	const IRValueView value = inst.operand(0);
	const IRValueView ptr = inst.operand(1);
	auto tmp = loadValue(value);
	storeToPointer(tmp.asUse(), ptr, value.type());
}

void InstructionSelector::translateBinary(const IRInstView & inst)
{
	auto * binaryInst = dynamic_cast<BinaryInst *>(inst.raw());
	if (binaryInst == nullptr || inst.operandCount() < 2 || !inst.hasResult()) {
		return;
	}

	MachineOpcode opcode = MachineOpcode::ADDW;
	switch (binaryInst->getBinaryOp()) {
		case BinaryInst::Op::Add:
			opcode = MachineOpcode::ADDW;
			break;
		case BinaryInst::Op::Sub:
			opcode = MachineOpcode::SUBW;
			break;
		case BinaryInst::Op::Mul:
			opcode = MachineOpcode::MULW;
			break;
		case BinaryInst::Op::SDiv:
			opcode = MachineOpcode::DIVW;
			break;
		case BinaryInst::Op::SRem:
			opcode = MachineOpcode::REMW;
			break;
		default:
			return;
	}

	auto lhs = loadValue(inst.operand(0));
	auto rhs = loadValue(inst.operand(1));
	auto result = newVRegDef();
	machineFunction.emit(opcode, {result, lhs.asUse(), rhs.asUse()});
	storeValue(result.asUse(), inst.result());
}

void InstructionSelector::translateICmp(const IRInstView & inst)
{
	auto * cmpInst = dynamic_cast<ICmpInst *>(inst.raw());
	if (cmpInst == nullptr || inst.operandCount() < 2 || !inst.hasResult()) {
		return;
	}

	auto lhs = loadValue(inst.operand(0));
	auto rhs = loadValue(inst.operand(1));
	auto result = newVRegDef();

	switch (cmpInst->getPredicate()) {
		case ICmpInst::Predicate::EQ:
			machineFunction.emit(MachineOpcode::XOR, {result, lhs.asUse(), rhs.asUse()});
			machineFunction.emit(MachineOpcode::SEQZ, {result.asDef(), result.asUse()});
			break;
		case ICmpInst::Predicate::NE:
			machineFunction.emit(MachineOpcode::XOR, {result, lhs.asUse(), rhs.asUse()});
			machineFunction.emit(MachineOpcode::SNEZ, {result.asDef(), result.asUse()});
			break;
		case ICmpInst::Predicate::SLT:
			machineFunction.emit(MachineOpcode::SLT, {result, lhs.asUse(), rhs.asUse()});
			break;
		case ICmpInst::Predicate::SGT:
			machineFunction.emit(MachineOpcode::SLT, {result, rhs.asUse(), lhs.asUse()});
			break;
		case ICmpInst::Predicate::SLE:
			machineFunction.emit(MachineOpcode::SLT, {result, rhs.asUse(), lhs.asUse()});
			machineFunction.emit(MachineOpcode::XORI, {result.asDef(), result.asUse(), MachineOperand::immValue(1)});
			break;
		case ICmpInst::Predicate::SGE:
			machineFunction.emit(MachineOpcode::SLT, {result, lhs.asUse(), rhs.asUse()});
			machineFunction.emit(MachineOpcode::XORI, {result.asDef(), result.asUse(), MachineOperand::immValue(1)});
			break;
	}

	storeValue(result.asUse(), inst.result());
}

void InstructionSelector::translateZExt(const IRInstView & inst)
{
	if (inst.operandCount() < 1 || !inst.hasResult()) {
		return;
	}

	auto value = loadValue(inst.operand(0));
	machineFunction.emit(MachineOpcode::ANDI, {value.asDef(), value.asUse(), MachineOperand::immValue(1)});
	storeValue(value.asUse(), inst.result());
}

void InstructionSelector::translateGEP(const IRInstView & inst)
{
	auto * gepInst = dynamic_cast<GetElementPtrInst *>(inst.raw());
	if (gepInst == nullptr || !inst.hasResult()) {
		return;
	}

	auto address = newVRegDef();
	IRValueView base(gepInst->getBasePointer());
	loadAddress(base, address);

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
				machineFunction.emit(
					MachineOpcode::ADDI,
					{address.asDef(), address.asUse(), MachineOperand::immValue(offset)});
			}
			continue;
		}

		auto indexReg = loadValue(indexValue);
		auto scaleReg = newVRegDef();
		machineFunction.emit(MachineOpcode::LI, {scaleReg, MachineOperand::immValue(scale)});
		machineFunction.emit(MachineOpcode::MUL, {indexReg.asDef(), indexReg.asUse(), scaleReg.asUse()});
		machineFunction.emit(MachineOpcode::ADD, {address.asDef(), address.asUse(), indexReg.asUse()});
	}

	storeValue(address.asUse(), inst.result());
}

void InstructionSelector::translateCall(const IRInstView & inst)
{
	const std::size_t argCount = inst.operandCount();
	const std::size_t regArgCount = std::min<std::size_t>(argCount, FunctionFrameLayout::argRegCount);

	for (std::size_t index = 0; index < regArgCount; ++index) {
		auto arg = loadValue(inst.operand(index));
		machineFunction.emit(MachineOpcode::COPY, {MachineOperand::pregDef(REG_A[index]), arg.asUse()});
	}

	for (std::size_t index = regArgCount; index < argCount; ++index) {
		auto arg = loadValue(inst.operand(index));
		machineFunction.emit(
			MachineOpcode::SD,
			{arg.asUse(),
			 MachineOperand::mem(
				 PhysicalReg::SP,
				 static_cast<int64_t>((index - regArgCount) * FunctionFrameLayout::stackSlotSize))});
	}

	auto call = MachineInstr::make(MachineOpcode::CALL, {MachineOperand::functionSymbol(inst.calledFunctionName())});
	call.implicitDefs = {
		PhysicalReg::RA,
		PhysicalReg::A0,
		PhysicalReg::A1,
		PhysicalReg::A2,
		PhysicalReg::A3,
		PhysicalReg::A4,
		PhysicalReg::A5,
		PhysicalReg::A6,
		PhysicalReg::A7,
		PhysicalReg::T0,
		PhysicalReg::T1,
		PhysicalReg::T2,
		PhysicalReg::T3,
		PhysicalReg::T4,
		PhysicalReg::T5,
		PhysicalReg::T6,
	};
	machineFunction.currentBlock()->emit(call);

	if (inst.hasResult()) {
		storeValue(MachineOperand::pregUse(PhysicalReg::A0), inst.result());
	}
}

void InstructionSelector::translateBranch(const IRInstView & inst)
{
	if (!inst.isConditionalBranch()) {
		machineFunction.emit(MachineOpcode::J, {MachineOperand::blockLabel(labelName(inst.targetBlockRaw()))});
		return;
	}

	auto cond = loadValue(inst.operand(0));
	machineFunction.emit(MachineOpcode::BNEZ, {cond.asUse(), MachineOperand::blockLabel(labelName(inst.trueBlockRaw()))});
	machineFunction.emit(MachineOpcode::J, {MachineOperand::blockLabel(labelName(inst.falseBlockRaw()))});
}

void InstructionSelector::translateReturn(const IRInstView & inst)
{
	if (inst.operandCount() > 0) {
		auto value = loadValue(inst.operand(0));
		machineFunction.emit(MachineOpcode::COPY, {MachineOperand::pregDef(PhysicalReg::A0), value.asUse()});
	}

	auto oldFp = newVRegDef();
	machineFunction.emit(
		MachineOpcode::LD,
		{MachineOperand::pregDef(PhysicalReg::RA),
		 MachineOperand::mem(PhysicalReg::FP, FunctionFrameLayout::savedRaOffset)});
	machineFunction.emit(
		MachineOpcode::LD,
		{oldFp, MachineOperand::mem(PhysicalReg::FP, FunctionFrameLayout::savedFpOffset)});
	machineFunction.emit(
		MachineOpcode::COPY,
		{MachineOperand::pregDef(PhysicalReg::SP), MachineOperand::pregUse(PhysicalReg::FP)});
	machineFunction.emit(
		MachineOpcode::COPY,
		{MachineOperand::pregDef(PhysicalReg::FP), oldFp.asUse()});
	machineFunction.emit(MachineOpcode::RET);
}

MachineOperand InstructionSelector::newVRegDef()
{
	return MachineOperand::vregDef(machineFunction.createVirtualReg(RegisterClass::GPR));
}

MachineOperand InstructionSelector::loadValue(const IRValueView & value)
{
	auto dst = newVRegDef();
	loadValueTo(value, dst);
	return dst;
}

void InstructionSelector::loadValueTo(const IRValueView & value, const MachineOperand & dst)
{
	if (!value.valid()) {
		machineFunction.emit(MachineOpcode::COPY, {dst.asDef(), MachineOperand::pregUse(PhysicalReg::Zero)});
		return;
	}

	if (value.isConstantInt()) {
		machineFunction.emit(MachineOpcode::LI, {dst.asDef(), MachineOperand::immValue(value.intValue())});
		return;
	}

	if (dynamic_cast<AllocaInst *>(value.raw()) != nullptr) {
		loadAddress(value, dst);
		return;
	}

	if (value.isGlobalVariable()) {
		auto address = newVRegDef();
		loadAddressOfGlobal(value, address);
		machineFunction.emit(loadOpcode(value.type()), {dst.asDef(), MachineOperand::memVReg(address.vreg, 0)});
		return;
	}

	const auto * slot = slotOf(value);
	if (slot != nullptr) {
		(void) slot;
		machineFunction.emit(loadOpcode(value.type()), {dst.asDef(), MachineOperand::stackSlot(value.raw())});
	}
}

void InstructionSelector::storeValue(const MachineOperand & src, const IRValueView & value)
{
	if (!value.valid()) {
		return;
	}

	if (value.isGlobalVariable()) {
		auto address = newVRegDef();
		loadAddressOfGlobal(value, address);
		machineFunction.emit(storeOpcode(value.type()), {src.asUse(), MachineOperand::memVReg(address.vreg, 0)});
		return;
	}

	const auto * slot = slotOf(value);
	if (slot != nullptr) {
		(void) slot;
		machineFunction.emit(storeOpcode(value.type()), {src.asUse(), MachineOperand::stackSlot(value.raw())});
	}
}

void InstructionSelector::loadAddress(const IRValueView & value, const MachineOperand & dst)
{
	if (!value.valid()) {
		machineFunction.emit(MachineOpcode::COPY, {dst.asDef(), MachineOperand::pregUse(PhysicalReg::Zero)});
		return;
	}

	if (value.isGlobalVariable()) {
		loadAddressOfGlobal(value, dst);
		return;
	}

	if (dynamic_cast<AllocaInst *>(value.raw()) != nullptr) {
		const auto * slot = slotOf(value);
		if (slot != nullptr) {
			(void) slot;
			machineFunction.emit(MachineOpcode::LA_STACK, {dst.asDef(), MachineOperand::stackSlot(value.raw())});
		}
		return;
	}

	loadValueTo(value, dst);
}

void InstructionSelector::loadFromPointer(const IRValueView & ptr, Type * valueType, const MachineOperand & dst)
{
	if (dynamic_cast<AllocaInst *>(ptr.raw()) != nullptr) {
		const auto * slot = slotOf(ptr);
		if (slot != nullptr) {
			(void) slot;
			machineFunction.emit(loadOpcode(valueType), {dst.asDef(), MachineOperand::stackSlot(ptr.raw())});
		}
		return;
	}

	if (ptr.isGlobalVariable()) {
		auto address = newVRegDef();
		loadAddressOfGlobal(ptr, address);
		machineFunction.emit(loadOpcode(valueType), {dst.asDef(), MachineOperand::memVReg(address.vreg, 0)});
		return;
	}

	auto address = loadValue(ptr);
	machineFunction.emit(loadOpcode(valueType), {dst.asDef(), MachineOperand::memVReg(address.vreg, 0)});
}

void InstructionSelector::storeToPointer(const MachineOperand & src, const IRValueView & ptr, Type * valueType)
{
	if (dynamic_cast<AllocaInst *>(ptr.raw()) != nullptr) {
		const auto * slot = slotOf(ptr);
		if (slot != nullptr) {
			(void) slot;
			machineFunction.emit(storeOpcode(valueType), {src.asUse(), MachineOperand::stackSlot(ptr.raw())});
		}
		return;
	}

	if (ptr.isGlobalVariable()) {
		auto address = newVRegDef();
		loadAddressOfGlobal(ptr, address);
		machineFunction.emit(storeOpcode(valueType), {src.asUse(), MachineOperand::memVReg(address.vreg, 0)});
		return;
	}

	auto address = loadValue(ptr);
	machineFunction.emit(storeOpcode(valueType), {src.asUse(), MachineOperand::memVReg(address.vreg, 0)});
}

void InstructionSelector::loadAddressOfGlobal(const IRValueView & value, const MachineOperand & dst)
{
	machineFunction.emit(MachineOpcode::LA, {dst.asDef(), MachineOperand::globalSymbol(value.name())});
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

MachineOpcode InstructionSelector::loadOpcode(Type * type) const
{
	return isEightByteType(type) ? MachineOpcode::LD : MachineOpcode::LW;
}

MachineOpcode InstructionSelector::storeOpcode(Type * type) const
{
	return isEightByteType(type) ? MachineOpcode::SD : MachineOpcode::SW;
}
