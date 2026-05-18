#include "InstructionSelector.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <utility>

#include "BasicBlock.h"
#include "Function.h"
#include "Type.h"
#include "AllocaInst.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CastInst.h"
#include "FCmpInst.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "PhiInst.h"
#include "ArrayType.h"
#include "PointerType.h"
#include "Use.h"
#include "ZeroInitializer.h"

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

constexpr PhysicalReg REG_FA[] = {
	PhysicalReg::FA0,
	PhysicalReg::FA1,
	PhysicalReg::FA2,
	PhysicalReg::FA3,
	PhysicalReg::FA4,
	PhysicalReg::FA5,
	PhysicalReg::FA6,
	PhysicalReg::FA7,
};

uint32_t floatBits(float value)
{
	uint32_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

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

std::string asmSymbolFromIRName(const std::string & irName, const std::string & fallback)
{
	const std::string & symbol = irName.empty() ? fallback : irName;
	return !symbol.empty() && symbol.front() == '@' ? symbol.substr(1) : symbol;
}

bool isPowerOfTwo(int32_t value)
{
	return value > 0 && (value & (value - 1)) == 0;
}

bool isSigned12Bit(int64_t value)
{
	return value >= -2048 && value <= 2047;
}

int32_t log2Int(int32_t value)
{
	int32_t shift = 0;
	while (value > 1) {
		value >>= 1;
		++shift;
	}
	return shift;
}

bool isOnlyUsedByConditionalBranches(Instruction * inst)
{
	if (inst == nullptr || inst->getUseList().empty()) {
		return false;
	}

	for (auto * use: inst->getUseList()) {
		auto * branch = dynamic_cast<BranchInst *>(use->getUser());
		if (branch == nullptr || !branch->isConditional()) {
			return false;
		}
	}
	return true;
}

bool blockContainsInstruction(BasicBlock * block, Instruction * inst)
{
	if (block == nullptr || inst == nullptr) {
		return false;
	}

	for (auto * blockInst: block->getInstructions()) {
		if (blockInst == inst) {
			return true;
		}
	}
	return false;
}

bool isOnlyUsedByLocalConditionalBranches(Instruction * inst, BasicBlock * block)
{
	if (!isOnlyUsedByConditionalBranches(inst)) {
		return false;
	}

	for (auto * use: inst->getUseList()) {
		auto * branch = dynamic_cast<BranchInst *>(use->getUser());
		if (!blockContainsInstruction(block, branch)) {
			return false;
		}
	}
	return true;
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

	currentIRBlock = block.raw();
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
			if (isOnlyUsedByLocalConditionalBranches(inst.raw(), currentIRBlock)) {
				break;
			}
			translateICmp(inst);
			break;
		case IRInstKind::FCmp:
			translateFCmp(inst);
			break;
		case IRInstKind::ZExt:
			translateZExt(inst);
			break;
		case IRInstKind::Cast:
			translateCast(inst);
			break;
		case IRInstKind::GetElementPtr:
			translateGEP(inst);
			break;
		case IRInstKind::Call:
			translateCall(inst);
			break;
		case IRInstKind::Phi:
			translatePhi(inst);
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
	std::size_t gprIndex = 0;
	std::size_t fprIndex = 0;
	std::size_t stackIndex = 0;
	for (const auto & param: params) {
		if (regClassForType(param.type()) == RegisterClass::FPR && fprIndex < FunctionFrameLayout::argRegCount) {
			storeValue(MachineOperand::pregUse(REG_FA[fprIndex++]), param);
			continue;
		}
		if (regClassForType(param.type()) == RegisterClass::GPR && gprIndex < FunctionFrameLayout::argRegCount) {
			storeValue(MachineOperand::pregUse(REG_A[gprIndex++]), param);
			continue;
		}
		const int64_t callerArgOffset = static_cast<int64_t>(stackIndex++ * FunctionFrameLayout::stackSlotSize);
		auto tmp = newVRegDef(param.type());
		machineFunction.emit(
			loadOpcode(param.type()),
			{tmp, MachineOperand::mem(PhysicalReg::FP, callerArgOffset)});
		storeValue(tmp.asUse(), param);
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

	auto tmp = newVRegDef(inst.type());
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
	if (dynamic_cast<ZeroInitializer *>(value.raw()) != nullptr) {
		storeZeroInitializer(ptr, value.type());
		return;
	}
	auto tmp = loadValue(value);
	storeToPointer(tmp.asUse(), ptr, value.type());
}

void InstructionSelector::translateBinary(const IRInstView & inst)
{
	auto * binaryInst = dynamic_cast<BinaryInst *>(inst.raw());
	if (binaryInst == nullptr || inst.operandCount() < 2 || !inst.hasResult()) {
		return;
	}

	if (inst.type() != nullptr && inst.type()->isInt32Type()) {
		const auto lhsValue = inst.operand(0);
		const auto rhsValue = inst.operand(1);
		const auto op = binaryInst->getBinaryOp();

		if (op == BinaryInst::Op::Add) {
			if (rhsValue.isConstantInt() && isSigned12Bit(rhsValue.intValue())) {
				auto lhs = loadValue(lhsValue);
				auto result = newVRegDef(inst.type());
				machineFunction.emit(
					MachineOpcode::ADDIW,
					{result, lhs.asUse(), MachineOperand::immValue(rhsValue.intValue())});
				storeValue(result.asUse(), inst.result());
				return;
			}
			if (lhsValue.isConstantInt() && isSigned12Bit(lhsValue.intValue())) {
				auto rhs = loadValue(rhsValue);
				auto result = newVRegDef(inst.type());
				machineFunction.emit(
					MachineOpcode::ADDIW,
					{result, rhs.asUse(), MachineOperand::immValue(lhsValue.intValue())});
				storeValue(result.asUse(), inst.result());
				return;
			}
		}

		if (op == BinaryInst::Op::Sub && rhsValue.isConstantInt()) {
			const int64_t negated = -static_cast<int64_t>(rhsValue.intValue());
			if (isSigned12Bit(negated)) {
				auto lhs = loadValue(lhsValue);
				auto result = newVRegDef(inst.type());
				machineFunction.emit(MachineOpcode::ADDIW, {result, lhs.asUse(), MachineOperand::immValue(negated)});
				storeValue(result.asUse(), inst.result());
				return;
			}
		}
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
		case BinaryInst::Op::FAdd:
			opcode = MachineOpcode::FADD_S;
			break;
		case BinaryInst::Op::FSub:
			opcode = MachineOpcode::FSUB_S;
			break;
		case BinaryInst::Op::FMul:
			opcode = MachineOpcode::FMUL_S;
			break;
		case BinaryInst::Op::FDiv:
			opcode = MachineOpcode::FDIV_S;
			break;
		default:
			return;
	}

	auto lhs = loadValue(inst.operand(0));
	auto rhs = loadValue(inst.operand(1));
	auto result = newVRegDef(inst.type());
	machineFunction.emit(opcode, {result, lhs.asUse(), rhs.asUse()});
	storeValue(result.asUse(), inst.result());
}

void InstructionSelector::translateICmp(const IRInstView & inst)
{
	auto * cmpInst = dynamic_cast<ICmpInst *>(inst.raw());
	if (cmpInst == nullptr || inst.operandCount() < 2 || !inst.hasResult()) {
		return;
	}

	auto loadCompareOperand = [&](const IRValueView & value) {
		if (value.isConstantInt() && value.intValue() == 0) {
			return MachineOperand::pregUse(PhysicalReg::Zero);
		}
		return loadValue(value).asUse();
	};

	auto lhs = loadCompareOperand(inst.operand(0));
	auto rhs = loadCompareOperand(inst.operand(1));
	auto result = newVRegDef();

	switch (cmpInst->getPredicate()) {
		case ICmpInst::Predicate::EQ:
			machineFunction.emit(MachineOpcode::XOR, {result, lhs, rhs});
			machineFunction.emit(MachineOpcode::SEQZ, {result.asDef(), result.asUse()});
			break;
		case ICmpInst::Predicate::NE:
			machineFunction.emit(MachineOpcode::XOR, {result, lhs, rhs});
			machineFunction.emit(MachineOpcode::SNEZ, {result.asDef(), result.asUse()});
			break;
		case ICmpInst::Predicate::SLT:
			machineFunction.emit(MachineOpcode::SLT, {result, lhs, rhs});
			break;
		case ICmpInst::Predicate::SGT:
			machineFunction.emit(MachineOpcode::SLT, {result, rhs, lhs});
			break;
		case ICmpInst::Predicate::SLE:
			machineFunction.emit(MachineOpcode::SLT, {result, rhs, lhs});
			machineFunction.emit(MachineOpcode::XORI, {result.asDef(), result.asUse(), MachineOperand::immValue(1)});
			break;
		case ICmpInst::Predicate::SGE:
			machineFunction.emit(MachineOpcode::SLT, {result, lhs, rhs});
			machineFunction.emit(MachineOpcode::XORI, {result.asDef(), result.asUse(), MachineOperand::immValue(1)});
			break;
	}

	storeValue(result.asUse(), inst.result());
}

void InstructionSelector::translateFCmp(const IRInstView & inst)
{
	auto * cmpInst = dynamic_cast<FCmpInst *>(inst.raw());
	if (cmpInst == nullptr || inst.operandCount() < 2 || !inst.hasResult()) {
		return;
	}

	auto lhs = loadValue(inst.operand(0));
	auto rhs = loadValue(inst.operand(1));
	auto result = newVRegDef(RegisterClass::GPR);

	switch (cmpInst->getPredicate()) {
		case FCmpInst::Predicate::OEQ:
			machineFunction.emit(MachineOpcode::FEQ_S, {result, lhs.asUse(), rhs.asUse()});
			break;
		case FCmpInst::Predicate::ONE:
			machineFunction.emit(MachineOpcode::FEQ_S, {result, lhs.asUse(), rhs.asUse()});
			machineFunction.emit(MachineOpcode::XORI, {result.asDef(), result.asUse(), MachineOperand::immValue(1)});
			break;
		case FCmpInst::Predicate::OLT:
			machineFunction.emit(MachineOpcode::FLT_S, {result, lhs.asUse(), rhs.asUse()});
			break;
		case FCmpInst::Predicate::OLE:
			machineFunction.emit(MachineOpcode::FLE_S, {result, lhs.asUse(), rhs.asUse()});
			break;
		case FCmpInst::Predicate::OGT:
			machineFunction.emit(MachineOpcode::FLT_S, {result, rhs.asUse(), lhs.asUse()});
			break;
		case FCmpInst::Predicate::OGE:
			machineFunction.emit(MachineOpcode::FLE_S, {result, rhs.asUse(), lhs.asUse()});
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

void InstructionSelector::translateCast(const IRInstView & inst)
{
	auto * castInst = dynamic_cast<CastInst *>(inst.raw());
	if (castInst == nullptr || inst.operandCount() < 1 || !inst.hasResult()) {
		return;
	}

	auto value = loadValue(inst.operand(0));
	auto result = newVRegDef(inst.type());
	switch (castInst->getCastOp()) {
		case CastInst::Op::SIToFP:
			machineFunction.emit(MachineOpcode::FCVT_S_W, {result, value.asUse()});
			break;
		case CastInst::Op::FPToSI:
			machineFunction.emit(
				MachineOpcode::FCVT_W_S,
				{result, value.asUse(), MachineOperand::functionSymbol("rtz")});
			break;
		case CastInst::Op::BitCast:
			machineFunction.emit(MachineOpcode::COPY, {result, value.asUse()});
			break;
	}
	storeValue(result.asUse(), inst.result());
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
		if (scale != 1) {
			if (isPowerOfTwo(scale)) {
				machineFunction.emit(
					MachineOpcode::SLLI,
					{indexReg.asDef(), indexReg.asUse(), MachineOperand::immValue(log2Int(scale))});
			} else {
				auto scaleReg = newVRegDef();
				machineFunction.emit(MachineOpcode::LI, {scaleReg, MachineOperand::immValue(scale)});
				machineFunction.emit(MachineOpcode::MUL, {indexReg.asDef(), indexReg.asUse(), scaleReg.asUse()});
			}
		}
		machineFunction.emit(MachineOpcode::ADD, {address.asDef(), address.asUse(), indexReg.asUse()});
	}

	storeValue(address.asUse(), inst.result());
}

void InstructionSelector::translateCall(const IRInstView & inst)
{
	const std::size_t argCount = inst.operandCount();
	std::size_t gprIndex = 0;
	std::size_t fprIndex = 0;
	std::size_t stackIndex = 0;

	for (std::size_t index = 0; index < argCount; ++index) {
		auto arg = loadValue(inst.operand(index));
		if (regClassForType(inst.operand(index).type()) == RegisterClass::FPR && fprIndex < FunctionFrameLayout::argRegCount) {
			machineFunction.emit(MachineOpcode::COPY, {MachineOperand::pregDef(REG_FA[fprIndex++]), arg.asUse()});
			continue;
		}
		if (regClassForType(inst.operand(index).type()) == RegisterClass::GPR && gprIndex < FunctionFrameLayout::argRegCount) {
			machineFunction.emit(MachineOpcode::COPY, {MachineOperand::pregDef(REG_A[gprIndex++]), arg.asUse()});
			continue;
		}
		machineFunction.emit(
			storeOpcode(inst.operand(index).type()),
			{arg.asUse(),
			 MachineOperand::mem(
				 PhysicalReg::SP,
				 static_cast<int64_t>(stackIndex++ * FunctionFrameLayout::stackSlotSize))});
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
		PhysicalReg::FA0,
		PhysicalReg::FA1,
		PhysicalReg::FA2,
		PhysicalReg::FA3,
		PhysicalReg::FA4,
		PhysicalReg::FA5,
		PhysicalReg::FA6,
		PhysicalReg::FA7,
		PhysicalReg::FT0,
		PhysicalReg::FT1,
		PhysicalReg::FT2,
		PhysicalReg::FT3,
		PhysicalReg::FT4,
		PhysicalReg::FT5,
		PhysicalReg::FT6,
		PhysicalReg::FT7,
		PhysicalReg::FT8,
		PhysicalReg::FT9,
		PhysicalReg::FT10,
		PhysicalReg::FT11,
	};
	machineFunction.currentBlock()->emit(call);

	if (inst.hasResult()) {
		const PhysicalReg returnReg = regClassForType(inst.type()) == RegisterClass::FPR ? PhysicalReg::FA0 : PhysicalReg::A0;
		storeValue(MachineOperand::pregUse(returnReg), inst.result());
	}
}

void InstructionSelector::translatePhi(const IRInstView &)
{
	// Phi copies are emitted on incoming control-flow edges in translateBranch().
}

void InstructionSelector::translateBranch(const IRInstView & inst)
{
	if (!inst.isConditionalBranch()) {
		emitPhiCopies(inst.targetBlockRaw(), currentIRBlock);
		machineFunction.emit(MachineOpcode::J, {MachineOperand::blockLabel(labelName(inst.targetBlockRaw()))});
		return;
	}

	BasicBlock * trueTarget = inst.trueBlockRaw();
	BasicBlock * falseTarget = inst.falseBlockRaw();
	const bool trueNeedsCopies = hasPhiCopiesForEdge(trueTarget, currentIRBlock);
	const std::string trueLabel = trueNeedsCopies ? edgeCopyLabel(currentIRBlock, trueTarget) : labelName(trueTarget);

	auto loadBranchOperand = [&](const IRValueView & value) {
		if (value.isConstantInt() && value.intValue() == 0) {
			return MachineOperand::pregUse(PhysicalReg::Zero);
		}
		return loadValue(value).asUse();
	};

	auto * cmpInst = dynamic_cast<ICmpInst *>(inst.operand(0).raw());
	const bool canFuseCmpBranch = blockContainsInstruction(currentIRBlock, cmpInst);
	if (canFuseCmpBranch && cmpInst != nullptr && cmpInst->getOperandsNum() == 2) {
		auto lhs = loadBranchOperand(IRValueView(cmpInst->getOperand(0)));
		auto rhs = loadBranchOperand(IRValueView(cmpInst->getOperand(1)));
		MachineOpcode branchOpcode = MachineOpcode::BNE;
		switch (cmpInst->getPredicate()) {
			case ICmpInst::Predicate::EQ:
				branchOpcode = MachineOpcode::BEQ;
				break;
			case ICmpInst::Predicate::NE:
				branchOpcode = MachineOpcode::BNE;
				break;
			case ICmpInst::Predicate::SLT:
				branchOpcode = MachineOpcode::BLT;
				break;
			case ICmpInst::Predicate::SGT:
				branchOpcode = MachineOpcode::BLT;
				std::swap(lhs, rhs);
				break;
			case ICmpInst::Predicate::SLE:
				branchOpcode = MachineOpcode::BGE;
				std::swap(lhs, rhs);
				break;
			case ICmpInst::Predicate::SGE:
				branchOpcode = MachineOpcode::BGE;
				break;
		}
		machineFunction.emit(branchOpcode, {lhs, rhs, MachineOperand::blockLabel(trueLabel)});
	} else {
		auto cond = loadValue(inst.operand(0));
		machineFunction.emit(MachineOpcode::BNEZ, {cond.asUse(), MachineOperand::blockLabel(trueLabel)});
	}

	// The false edge is the fall-through path after the conditional branch, so its copies can be emitted inline.
	emitPhiCopies(falseTarget, currentIRBlock);
	machineFunction.emit(MachineOpcode::J, {MachineOperand::blockLabel(labelName(falseTarget))});

	if (trueNeedsCopies) {
		machineFunction.createBlock(trueLabel);
		emitPhiCopies(trueTarget, currentIRBlock);
		machineFunction.emit(MachineOpcode::J, {MachineOperand::blockLabel(labelName(trueTarget))});
	}
}

void InstructionSelector::translateReturn(const IRInstView & inst)
{
	if (inst.operandCount() > 0) {
		auto value = loadValue(inst.operand(0));
		const PhysicalReg returnReg =
			regClassForType(inst.operand(0).type()) == RegisterClass::FPR ? PhysicalReg::FA0 : PhysicalReg::A0;
		machineFunction.emit(MachineOpcode::COPY, {MachineOperand::pregDef(returnReg), value.asUse()});
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

MachineOperand InstructionSelector::newVRegDef(RegisterClass regClass)
{
	return MachineOperand::vregDef(machineFunction.createVirtualReg(regClass), regClass);
}

MachineOperand InstructionSelector::newVRegDef(Type * type)
{
	return newVRegDef(regClassForType(type));
}

MachineOperand InstructionSelector::loadValue(const IRValueView & value)
{
	auto dst = newVRegDef(value.type());
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

	if (value.isConstantFloat()) {
		auto bits = newVRegDef(RegisterClass::GPR);
		machineFunction.emit(MachineOpcode::LI, {bits, MachineOperand::immValue(floatBits(value.floatValue()))});
		machineFunction.emit(MachineOpcode::FMV_W_X, {dst.asDef(), bits.asUse()});
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

void InstructionSelector::storeZeroInitializer(const IRValueView & ptr, Type * valueType)
{
	if (valueType == nullptr || valueType->getSize() <= 0) {
		return;
	}

	auto address = newVRegDef(RegisterClass::GPR);
	loadAddress(ptr, address);
	for (int32_t offset = 0; offset < valueType->getSize(); offset += 4) {
		machineFunction.emit(
			MachineOpcode::SW,
			{MachineOperand::pregUse(PhysicalReg::Zero), MachineOperand::memVReg(address.vreg, offset)});
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
	machineFunction.emit(
		MachineOpcode::LA,
		{dst.asDef(), MachineOperand::globalSymbol(asmSymbolFromIRName(value.irName(), value.name()))});
}

bool InstructionSelector::hasPhiCopiesForEdge(BasicBlock * successor, BasicBlock * predecessor) const
{
	if (successor == nullptr || predecessor == nullptr) {
		return false;
	}

	for (auto * inst: successor->getInstructions()) {
		auto * phi = dynamic_cast<PhiInst *>(inst);
		if (phi == nullptr) {
			break;
		}
		for (const auto & incoming: phi->getIncomingValues()) {
			if (incoming.second == predecessor) {
				return true;
			}
		}
	}

	return false;
}

void InstructionSelector::emitPhiCopies(BasicBlock * successor, BasicBlock * predecessor)
{
	if (successor == nullptr || predecessor == nullptr) {
		return;
	}

	std::vector<std::pair<Value *, MachineOperand>> copies;
	for (auto * inst: successor->getInstructions()) {
		auto * phi = dynamic_cast<PhiInst *>(inst);
		if (phi == nullptr) {
			break;
		}

		for (const auto & incoming: phi->getIncomingValues()) {
			if (incoming.second != predecessor) {
				continue;
			}
			// Load every source before writing any destination: phi lowering is a parallel copy.
			copies.emplace_back(phi, loadValue(IRValueView(incoming.first)).asUse());
			break;
		}
	}

	for (const auto & copy: copies) {
		storeValue(copy.second, IRValueView(copy.first));
	}
}

std::string InstructionSelector::edgeCopyLabel(BasicBlock * from, BasicBlock * to)
{
	std::string label = ".L_" + sanitizeLabelPart(function.name()) + "_phi_edge_" + std::to_string(nextLabelIndex++);
	if (from != nullptr) {
		label += "_" + sanitizeLabelPart(from->getIRName());
	}
	if (to != nullptr) {
		label += "_to_" + sanitizeLabelPart(to->getIRName());
	}
	return label;
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

bool InstructionSelector::isFloatType(Type * type) const
{
	return type != nullptr && type->isFloatType();
}

RegisterClass InstructionSelector::regClassForType(Type * type) const
{
	return isFloatType(type) ? RegisterClass::FPR : RegisterClass::GPR;
}

MachineOpcode InstructionSelector::loadOpcode(Type * type) const
{
	if (isFloatType(type)) {
		return MachineOpcode::FLW;
	}
	return isEightByteType(type) ? MachineOpcode::LD : MachineOpcode::LW;
}

MachineOpcode InstructionSelector::storeOpcode(Type * type) const
{
	if (isFloatType(type)) {
		return MachineOpcode::FSW;
	}
	return isEightByteType(type) ? MachineOpcode::SD : MachineOpcode::SW;
}
