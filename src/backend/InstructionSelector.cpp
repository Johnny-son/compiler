#include "InstructionSelector.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "BasicBlock.h"
#include "Function.h"
#include "Type.h"
#include "AllocaInst.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CastInst.h"
#include "ConstInt.h"
#include "FCmpInst.h"
#include "FormalParam.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "StoreInst.h"
#include "ArrayType.h"
#include "PointerType.h"
#include "Use.h"
#include "ZeroInitializer.h"
#include "ZExtInst.h"

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

std::string gepValueKey(Value * value)
{
	if (value == nullptr) {
		return "null";
	}
	if (auto * constant = dynamic_cast<ConstInt *>(value); constant != nullptr) {
		return "ci:" + std::to_string(constant->getVal());
	}
	return "v:" + std::to_string(reinterpret_cast<std::uintptr_t>(value));
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

bool absModuloTwoDivisor(int32_t value, int32_t & absValue)
{
	absValue = value < 0 ? -value : value;
	return absValue == 2;
}

int64_t alignTo(int64_t value, int64_t alignment)
{
	return (value + alignment - 1) / alignment * alignment;
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

bool isFloatValue(Value * value)
{
	return value != nullptr && value->getType() != nullptr && value->getType()->isFloatType();
}

int32_t wrapI32(int64_t value)
{
	return static_cast<int32_t>(static_cast<uint32_t>(value));
}

bool readI32Value(Value * value, const std::unordered_map<Value *, int32_t> & values, int32_t & result)
{
	if (auto * constant = dynamic_cast<ConstInt *>(value); constant != nullptr) {
		result = constant->getVal();
		return true;
	}

	auto iter = values.find(value);
	if (iter == values.end()) {
		return false;
	}
	result = iter->second;
	return true;
}

constexpr int pureI32EvalStepLimit = 1000000;
constexpr std::size_t pureI32EvalArgLimit = 64;

bool evalPureI32Function(Function * callee, const std::vector<int32_t> & args, int32_t & result, int depth = 0)
{
	if (callee == nullptr || callee->isBuiltin() || callee->getReturnType() == nullptr ||
		!callee->getReturnType()->isInt32Type()) {
		return false;
	}
	if (depth > 64) {
		return false;
	}

	auto & params = callee->getParams();
	if (params.size() != args.size()) {
		return false;
	}

	std::unordered_map<Value *, int32_t> values;
	std::unordered_map<Value *, int32_t> memory;
	for (std::size_t index = 0; index < params.size(); ++index) {
		if (params[index] == nullptr || params[index]->getType() == nullptr || !params[index]->getType()->isInt32Type()) {
			return false;
		}
		values[params[index]] = args[index];
	}

	BasicBlock * block = callee->getEntryBlock();
	BasicBlock * predecessor = nullptr;
	int steps = 0;
	while (block != nullptr && steps++ < pureI32EvalStepLimit) {
		const auto & instructions = block->getInstructions();
		std::vector<std::pair<Value *, int32_t>> phiWrites;
		std::size_t firstNonPhi = 0;
		for (; firstNonPhi < instructions.size(); ++firstNonPhi) {
			auto * phi = dynamic_cast<PhiInst *>(instructions[firstNonPhi]);
			if (phi == nullptr) {
				break;
			}

			bool matched = false;
			int32_t incomingValue = 0;
			for (const auto & incoming: phi->getIncomingValues()) {
				if (incoming.second != predecessor) {
					continue;
				}
				if (!readI32Value(incoming.first, values, incomingValue)) {
					return false;
				}
				matched = true;
				break;
			}
			if (!matched) {
				return false;
			}
			phiWrites.emplace_back(phi, incomingValue);
		}
		for (const auto & write: phiWrites) {
			values[write.first] = write.second;
		}

		bool jumped = false;
		for (std::size_t index = firstNonPhi; index < instructions.size(); ++index) {
			auto * inst = instructions[index];
			if (inst == nullptr || steps++ >= pureI32EvalStepLimit) {
				return false;
			}

			if (auto * binary = dynamic_cast<BinaryInst *>(inst); binary != nullptr) {
				if (binary->getOperandsNum() != 2 || binary->getType() == nullptr || !binary->getType()->isInt32Type()) {
					return false;
				}
				int32_t lhs = 0;
				int32_t rhs = 0;
				if (!readI32Value(binary->getOperand(0), values, lhs) ||
					!readI32Value(binary->getOperand(1), values, rhs)) {
					return false;
				}
				switch (binary->getBinaryOp()) {
					case BinaryInst::Op::Add:
						values[binary] = wrapI32(static_cast<int64_t>(lhs) + rhs);
						break;
					case BinaryInst::Op::Sub:
						values[binary] = wrapI32(static_cast<int64_t>(lhs) - rhs);
						break;
					case BinaryInst::Op::Mul:
						values[binary] = wrapI32(static_cast<int64_t>(lhs) * rhs);
						break;
					case BinaryInst::Op::SDiv:
						if (rhs == 0 || (lhs == std::numeric_limits<int32_t>::min() && rhs == -1)) {
							return false;
						}
						values[binary] = lhs / rhs;
						break;
					case BinaryInst::Op::SRem:
						if (rhs == 0 || (lhs == std::numeric_limits<int32_t>::min() && rhs == -1)) {
							return false;
						}
						values[binary] = lhs % rhs;
						break;
					default:
						return false;
				}
				continue;
			}

			if (auto * cmp = dynamic_cast<ICmpInst *>(inst); cmp != nullptr) {
				if (cmp->getOperandsNum() != 2) {
					return false;
				}
				int32_t lhs = 0;
				int32_t rhs = 0;
				if (!readI32Value(cmp->getOperand(0), values, lhs) || !readI32Value(cmp->getOperand(1), values, rhs)) {
					return false;
				}
				bool pred = false;
				switch (cmp->getPredicate()) {
					case ICmpInst::Predicate::EQ:
						pred = lhs == rhs;
						break;
					case ICmpInst::Predicate::NE:
						pred = lhs != rhs;
						break;
					case ICmpInst::Predicate::SLT:
						pred = lhs < rhs;
						break;
					case ICmpInst::Predicate::SLE:
						pred = lhs <= rhs;
						break;
					case ICmpInst::Predicate::SGT:
						pred = lhs > rhs;
						break;
					case ICmpInst::Predicate::SGE:
						pred = lhs >= rhs;
						break;
				}
				values[cmp] = pred ? 1 : 0;
				continue;
			}

			if (auto * zext = dynamic_cast<ZExtInst *>(inst); zext != nullptr) {
				int32_t value = 0;
				if (!readI32Value(zext->getSourceValue(), values, value)) {
					return false;
				}
				values[zext] = value != 0 ? 1 : 0;
				continue;
			}

			if (auto * cast = dynamic_cast<CastInst *>(inst); cast != nullptr) {
				if (cast->getCastOp() != CastInst::Op::BitCast || cast->getOperandsNum() != 1) {
					return false;
				}
				int32_t value = 0;
				if (!readI32Value(cast->getOperand(0), values, value)) {
					return false;
				}
				values[cast] = value;
				continue;
			}

			if (auto * alloca = dynamic_cast<AllocaInst *>(inst); alloca != nullptr) {
				if (alloca->getAllocatedType() == nullptr || !alloca->getAllocatedType()->isInt32Type()) {
					return false;
				}
				memory[alloca] = 0;
				continue;
			}

			if (auto * store = dynamic_cast<StoreInst *>(inst); store != nullptr) {
				int32_t stored = 0;
				Value * pointer = store->getPointerOperand();
				if (!readI32Value(store->getValueOperand(), values, stored) || memory.find(pointer) == memory.end()) {
					return false;
				}
				memory[pointer] = stored;
				continue;
			}

			if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
				auto iter = memory.find(load->getPointerOperand());
				if (iter == memory.end() || load->getType() == nullptr || !load->getType()->isInt32Type()) {
					return false;
				}
				values[load] = iter->second;
				continue;
			}

			if (auto * branch = dynamic_cast<BranchInst *>(inst); branch != nullptr) {
				predecessor = block;
				if (!branch->isConditional()) {
					block = branch->getTarget();
				} else {
					int32_t cond = 0;
					if (branch->getOperandsNum() != 1 || !readI32Value(branch->getOperand(0), values, cond)) {
						return false;
					}
					block = cond != 0 ? branch->getTrueTarget() : branch->getFalseTarget();
				}
				jumped = true;
				break;
			}

			if (auto * ret = dynamic_cast<ReturnInst *>(inst); ret != nullptr) {
				if (ret->getOperandsNum() != 1 || !readI32Value(ret->getOperand(0), values, result)) {
					return false;
				}
				return true;
			}

			if (auto * call = dynamic_cast<CallInst *>(inst); call != nullptr) {
				if (call->getCallee() != callee || !call->hasResultValue() || call->getType() == nullptr ||
					!call->getType()->isInt32Type()) {
					return false;
				}
				std::vector<int32_t> callArgs;
				callArgs.reserve(static_cast<std::size_t>(call->getOperandsNum()));
				for (int32_t argIndex = 0; argIndex < call->getOperandsNum(); ++argIndex) {
					int32_t argValue = 0;
					if (!readI32Value(call->getOperand(argIndex), values, argValue)) {
						return false;
					}
					callArgs.push_back(argValue);
				}
				int32_t callResult = 0;
				if (!evalPureI32Function(callee, callArgs, callResult, depth + 1)) {
					return false;
				}
				values[call] = callResult;
				continue;
			}

			if (dynamic_cast<FCmpInst *>(inst) != nullptr || dynamic_cast<GetElementPtrInst *>(inst) != nullptr ||
				dynamic_cast<LoadInst *>(inst) != nullptr || dynamic_cast<StoreInst *>(inst) != nullptr ||
				dynamic_cast<AllocaInst *>(inst) != nullptr) {
				return false;
			}
			return false;
		}

		if (!jumped) {
			return false;
		}
	}

	return false;
}

} // namespace

InstructionSelector::InstructionSelector(IRFunctionView function, const FunctionFrameLayout & layout)
	: function(function), frameLayout(layout), machineFunction(function.name())
{}

MachineFunction InstructionSelector::run()
{
	analyzeLocalValues();
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
	gepPrefixCache.clear();
	for (const auto & inst: block.instructions()) {
		translateInst(inst);
	}
}

void InstructionSelector::analyzeLocalValues()
{
	valueRegs.clear();
	phiValueRegs.clear();

	for (const auto & block: function.blocks()) {
		for (const auto & inst: block.instructions()) {
			if (dynamic_cast<PhiInst *>(inst.raw()) != nullptr && inst.hasResult()) {
				auto phiReg = newVRegDef(inst.type()).asUse();
				phiValueRegs.emplace(inst.raw(), phiReg);
				valueRegs.emplace(inst.raw(), phiReg);
			}
		}
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
			if (isOnlyUsedByConditionalBranches(inst.raw())) {
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
			auto dst = newVRegDef(param.type());
			auto copy = MachineInstr::make(MachineOpcode::COPY, {dst.asDef(), MachineOperand::pregUse(REG_FA[fprIndex])});
			for (std::size_t index = fprIndex + 1; index < FunctionFrameLayout::argRegCount; ++index) {
				copy.implicitDefs.push_back(REG_FA[index]);
			}
			machineFunction.currentBlock()->emit(copy);
			defineValue(param, dst.asUse());
			++fprIndex;
			continue;
		}
		if (regClassForType(param.type()) == RegisterClass::GPR && gprIndex < FunctionFrameLayout::argRegCount) {
			auto dst = newVRegDef(param.type());
			auto copy = MachineInstr::make(MachineOpcode::COPY, {dst.asDef(), MachineOperand::pregUse(REG_A[gprIndex])});
			for (std::size_t index = gprIndex + 1; index < FunctionFrameLayout::argRegCount; ++index) {
				copy.implicitDefs.push_back(REG_A[index]);
			}
			machineFunction.currentBlock()->emit(copy);
			defineValue(param, dst.asUse());
			++gprIndex;
			continue;
		}
		const int64_t callerArgOffset = static_cast<int64_t>(stackIndex++ * FunctionFrameLayout::stackSlotSize);
		stackParamOffsets[param.raw()] = callerArgOffset;
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
	defineValue(inst.result(), tmp.asUse());
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
	auto tmp = useValue(value);
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

		auto copyToResult = [&](const IRValueView & value) {
			auto loaded = useValue(value);
			defineValue(inst.result(), loaded.asUse());
		};

		if (op == BinaryInst::Op::Add) {
			if (rhsValue.isConstantInt() && rhsValue.intValue() == 0) {
				copyToResult(lhsValue);
				return;
			}
			if (lhsValue.isConstantInt() && lhsValue.intValue() == 0) {
				copyToResult(rhsValue);
				return;
			}
			if (rhsValue.isConstantInt() && isSigned12Bit(rhsValue.intValue())) {
				auto lhs = useValue(lhsValue);
				auto result = newVRegDef(inst.type());
				machineFunction.emit(
					MachineOpcode::ADDIW,
					{result, lhs.asUse(), MachineOperand::immValue(rhsValue.intValue())});
				defineValue(inst.result(), result.asUse());
				return;
			}
			if (lhsValue.isConstantInt() && isSigned12Bit(lhsValue.intValue())) {
				auto rhs = useValue(rhsValue);
				auto result = newVRegDef(inst.type());
				machineFunction.emit(
					MachineOpcode::ADDIW,
					{result, rhs.asUse(), MachineOperand::immValue(lhsValue.intValue())});
				defineValue(inst.result(), result.asUse());
				return;
			}
		}

		if (op == BinaryInst::Op::Sub && rhsValue.isConstantInt()) {
			if (rhsValue.intValue() == 0) {
				copyToResult(lhsValue);
				return;
			}
			const int64_t negated = -static_cast<int64_t>(rhsValue.intValue());
			if (isSigned12Bit(negated)) {
				auto lhs = useValue(lhsValue);
				auto result = newVRegDef(inst.type());
				machineFunction.emit(MachineOpcode::ADDIW, {result, lhs.asUse(), MachineOperand::immValue(negated)});
				defineValue(inst.result(), result.asUse());
				return;
			}
		}

		if (op == BinaryInst::Op::Mul && (lhsValue.isConstantInt() || rhsValue.isConstantInt())) {
			const IRValueView constValue = lhsValue.isConstantInt() ? lhsValue : rhsValue;
			const IRValueView variableValue = lhsValue.isConstantInt() ? rhsValue : lhsValue;
			const int32_t factor = constValue.intValue();
			if (factor == 0) {
				defineValue(inst.result(), MachineOperand::pregUse(PhysicalReg::Zero));
				return;
			}
			if (factor == 1) {
				copyToResult(variableValue);
				return;
			}
			auto value = useValue(variableValue);
			auto result = newVRegDef(inst.type());
			if (factor == -1) {
				machineFunction.emit(
					MachineOpcode::SUBW,
					{result, MachineOperand::pregUse(PhysicalReg::Zero), value.asUse()});
				defineValue(inst.result(), result.asUse());
				return;
			}
			if (isPowerOfTwo(factor)) {
				machineFunction.emit(
					MachineOpcode::SLLI,
					{result, value.asUse(), MachineOperand::immValue(log2Int(factor))});
				defineValue(inst.result(), result.asUse());
				return;
			}
		}

		if (op == BinaryInst::Op::SDiv && rhsValue.isConstantInt() && rhsValue.intValue() == 1) {
			copyToResult(lhsValue);
			return;
		}

		if (op == BinaryInst::Op::SRem && rhsValue.isConstantInt() &&
			(rhsValue.intValue() == 1 || rhsValue.intValue() == -1)) {
			defineValue(inst.result(), MachineOperand::pregUse(PhysicalReg::Zero));
			return;
		}

		if (op == BinaryInst::Op::SRem && isModuloZeroBranchRemainder(binaryInst)) {
			return;
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

	auto lhs = useValue(inst.operand(0));
	auto rhs = useValue(inst.operand(1));
	auto result = newVRegDef(inst.type());
	machineFunction.emit(opcode, {result, lhs.asUse(), rhs.asUse()});
	defineValue(inst.result(), result.asUse());
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
		return useValue(value).asUse();
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

	defineValue(inst.result(), result.asUse());
}

void InstructionSelector::translateFCmp(const IRInstView & inst)
{
	auto * cmpInst = dynamic_cast<FCmpInst *>(inst.raw());
	if (cmpInst == nullptr || inst.operandCount() < 2 || !inst.hasResult()) {
		return;
	}

	auto lhs = useValue(inst.operand(0));
	auto rhs = useValue(inst.operand(1));
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

	defineValue(inst.result(), result.asUse());
}

void InstructionSelector::translateZExt(const IRInstView & inst)
{
	if (inst.operandCount() < 1 || !inst.hasResult()) {
		return;
	}

	auto value = useValue(inst.operand(0));
	auto result = newVRegDef(inst.type());
	machineFunction.emit(MachineOpcode::ANDI, {result, value.asUse(), MachineOperand::immValue(1)});
	defineValue(inst.result(), result.asUse());
}

void InstructionSelector::translateCast(const IRInstView & inst)
{
	auto * castInst = dynamic_cast<CastInst *>(inst.raw());
	if (castInst == nullptr || inst.operandCount() < 1 || !inst.hasResult()) {
		return;
	}

	auto value = useValue(inst.operand(0));
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
	defineValue(inst.result(), result.asUse());
}

void InstructionSelector::translateGEP(const IRInstView & inst)
{
	auto * gepInst = dynamic_cast<GetElementPtrInst *>(inst.raw());
	if (gepInst == nullptr || !inst.hasResult()) {
		return;
	}

	IRValueView base(gepInst->getBasePointer());
	const auto indices = gepInst->getIndices();

	std::vector<std::string> prefixKeys;
	prefixKeys.reserve(indices.size());
	std::string prefixKey = "gep:" + gepValueKey(base.raw());
	for (auto * index: indices) {
		prefixKey += ":" + gepValueKey(index);
		prefixKeys.push_back(prefixKey);
	}

	MachineOperand address;
	std::size_t firstIndexToEmit = 0;
	bool foundCachedPrefix = false;
	for (std::size_t indexNo = prefixKeys.size(); indexNo > 0; --indexNo) {
		auto iter = gepPrefixCache.find(prefixKeys[indexNo - 1]);
		if (iter != gepPrefixCache.end()) {
			address = iter->second.asUse();
			firstIndexToEmit = indexNo;
			foundCachedPrefix = true;
			break;
		}
	}

	if (!foundCachedPrefix) {
		auto baseAddress = newVRegDef();
		loadAddress(base, baseAddress);
		address = baseAddress.asUse();
	}

	Type * currentType = pointeeType(base.raw());
	for (std::size_t indexNo = 0; indexNo < firstIndexToEmit; ++indexNo) {
		if (indexNo > 0) {
			if (auto * arrayType = dynamic_cast<ArrayType *>(currentType); arrayType != nullptr) {
				currentType = arrayType->getElementType();
			}
		}
	}

	for (std::size_t indexNo = firstIndexToEmit; indexNo < indices.size(); ++indexNo) {
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
			const int64_t offset = static_cast<int64_t>(indexValue.intValue()) * scale;
			if (offset != 0) {
				auto nextAddress = newVRegDef();
				machineFunction.emit(
					MachineOpcode::ADDI,
					{nextAddress, address.asUse(), MachineOperand::immValue(offset)});
				address = nextAddress.asUse();
			}
			gepPrefixCache[prefixKeys[indexNo]] = address.asUse();
			continue;
		}

		auto indexReg = useValue(indexValue);
		MachineOperand scaledIndex = indexReg.asUse();
		if (scale != 1) {
			if (isPowerOfTwo(scale)) {
				scaledIndex = newVRegDef();
				machineFunction.emit(
					MachineOpcode::SLLI,
					{scaledIndex.asDef(), indexReg.asUse(), MachineOperand::immValue(log2Int(scale))});
			} else {
				auto scaleReg = newVRegDef();
				machineFunction.emit(MachineOpcode::LI, {scaleReg, MachineOperand::immValue(scale)});
				scaledIndex = newVRegDef();
				machineFunction.emit(MachineOpcode::MUL, {scaledIndex.asDef(), indexReg.asUse(), scaleReg.asUse()});
			}
		}
		auto nextAddress = newVRegDef();
		machineFunction.emit(MachineOpcode::ADD, {nextAddress, address.asUse(), scaledIndex.asUse()});
		address = nextAddress.asUse();
		gepPrefixCache[prefixKeys[indexNo]] = address.asUse();
	}

	defineValue(inst.result(), address.asUse());
}

void InstructionSelector::translateCall(const IRInstView & inst)
{
		if (inst.hasResult() && inst.type() != nullptr && inst.type()->isInt32Type() &&
			inst.operandCount() <= pureI32EvalArgLimit) {
		auto * callInst = dynamic_cast<CallInst *>(inst.raw());
		Function * callee = callInst != nullptr ? callInst->getCallee() : nullptr;
		if (callee != nullptr && !callee->isBuiltin()) {
			std::vector<int32_t> constantArgs;
			constantArgs.reserve(inst.operandCount());
			bool allConstantInt = true;
			for (std::size_t index = 0; index < inst.operandCount(); ++index) {
				const IRValueView operand = inst.operand(index);
				if (!operand.isConstantInt()) {
					allConstantInt = false;
					break;
				}
				constantArgs.push_back(operand.intValue());
			}

			int32_t folded = 0;
			if (allConstantInt && evalPureI32Function(callee, constantArgs, folded)) {
				auto result = newVRegDef(inst.type());
				machineFunction.emit(MachineOpcode::LI, {result, MachineOperand::immValue(folded)});
				defineValue(inst.result(), result.asUse());
				return;
			}
		}
	}

	if (translateRecognizedHelperCall(inst)) {
		return;
	}

	const std::size_t argCount = inst.operandCount();
	std::size_t gprIndex = 0;
	std::size_t fprIndex = 0;
	std::size_t stackIndex = 0;
	struct RegisterArg {
		PhysicalReg reg;
		MachineOperand value;
		Type * type = nullptr;
	};
	struct StagedRegisterArg {
		PhysicalReg reg;
		Type * type = nullptr;
		int64_t offset = 0;
	};
	std::vector<RegisterArg> registerArgs;

	auto registerForArg = [&](Type * type) -> std::optional<PhysicalReg> {
		if (regClassForType(type) == RegisterClass::FPR && fprIndex < FunctionFrameLayout::argRegCount) {
			return REG_FA[fprIndex++];
		}
		if (regClassForType(type) == RegisterClass::GPR && gprIndex < FunctionFrameLayout::argRegCount) {
			return REG_A[gprIndex++];
		}
		return std::nullopt;
	};

	for (std::size_t index = 0; index < argCount; ++index) {
		(void) registerForArg(inst.operand(index).type());
	}
	const std::size_t stackArgCount = argCount - gprIndex - fprIndex;
	const bool useStackStaging = argCount > FunctionFrameLayout::argRegCount && frameLayout.outgoingArgAreaSize() > 0;
	const int64_t callAreaSize = useStackStaging
		? alignTo(static_cast<int64_t>(argCount * FunctionFrameLayout::stackSlotSize), 16)
		: 0;

	gprIndex = 0;
	fprIndex = 0;
	auto adjustStackPointer = [&](int64_t amount) {
		machineFunction.emit(
			MachineOpcode::LI,
			{MachineOperand::pregDef(PhysicalReg::T0), MachineOperand::immValue(amount)});
		machineFunction.emit(
			MachineOpcode::ADD,
			{MachineOperand::pregDef(PhysicalReg::SP),
			 MachineOperand::pregUse(PhysicalReg::SP),
			 MachineOperand::pregUse(PhysicalReg::T0)});
	};

	if (useStackStaging) {
		adjustStackPointer(-callAreaSize);

		std::vector<StagedRegisterArg> stagedRegisterArgs;
		stagedRegisterArgs.reserve(argCount - stackArgCount);
		std::size_t regStageIndex = 0;
		stackIndex = 0;

		for (std::size_t index = 0; index < argCount; ++index) {
			const IRValueView operand = inst.operand(index);
			auto arg = useValue(operand);
			auto argReg = registerForArg(operand.type());
			if (argReg.has_value()) {
				const int64_t stageOffset = static_cast<int64_t>(
					(stackArgCount + regStageIndex++) * FunctionFrameLayout::stackSlotSize);
				machineFunction.emit(
					storeOpcode(operand.type()),
					{arg.asUse(), MachineOperand::mem(PhysicalReg::SP, stageOffset)});
				stagedRegisterArgs.push_back(StagedRegisterArg{*argReg, operand.type(), stageOffset});
				continue;
			}

			machineFunction.emit(
				storeOpcode(operand.type()),
				{arg.asUse(),
				 MachineOperand::mem(
					 PhysicalReg::SP,
					 static_cast<int64_t>(stackIndex++ * FunctionFrameLayout::stackSlotSize))});
		}

		for (const auto & registerArg: stagedRegisterArgs) {
			machineFunction.emit(
				loadOpcode(registerArg.type),
				{MachineOperand::pregDef(registerArg.reg), MachineOperand::mem(PhysicalReg::SP, registerArg.offset)});
		}
	} else {
		stackIndex = 0;
		for (std::size_t index = 0; index < argCount; ++index) {
			const IRValueView operand = inst.operand(index);
			auto arg = useValue(operand);
			auto argReg = registerForArg(operand.type());
			if (argReg.has_value()) {
				auto tmp = newVRegDef(operand.type());
				machineFunction.emit(MachineOpcode::COPY, {tmp.asDef(), arg.asUse()});
				registerArgs.push_back(RegisterArg{*argReg, tmp.asUse(), operand.type()});
				continue;
			}
			machineFunction.emit(
				storeOpcode(operand.type()),
				{arg.asUse(),
				 MachineOperand::mem(
					 PhysicalReg::SP,
					 static_cast<int64_t>(stackIndex++ * FunctionFrameLayout::stackSlotSize))});
		}

		for (std::size_t index = 0; index < registerArgs.size(); ++index) {
			auto copy = MachineInstr::make(
				MachineOpcode::COPY,
				{MachineOperand::pregDef(registerArgs[index].reg), registerArgs[index].value.asUse()});
			for (std::size_t later = index + 1; later < registerArgs.size(); ++later) {
				copy.implicitDefs.push_back(registerArgs[later].reg);
			}
			machineFunction.currentBlock()->emit(copy);
		}
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

	if (callAreaSize > 0) {
		adjustStackPointer(callAreaSize);
	}

	if (inst.hasResult()) {
		const PhysicalReg returnReg = regClassForType(inst.type()) == RegisterClass::FPR ? PhysicalReg::FA0 : PhysicalReg::A0;
		defineValue(inst.result(), MachineOperand::pregUse(returnReg));
	}
	gepPrefixCache.clear();
}

bool InstructionSelector::translateRecognizedHelperCall(const IRInstView & inst)
{
	if (!inst.hasResult() || inst.type() == nullptr || !inst.type()->isInt32Type()) {
		return false;
	}

	const auto kind = classifyHelper(inst.calledFunctionRaw());
	if (kind == RecognizedHelperKind::None) {
		return false;
	}

	if (kind == RecognizedHelperKind::ConstZero) {
		defineValue(inst.result(), MachineOperand::pregUse(PhysicalReg::Zero));
		return true;
	}

	if (kind == RecognizedHelperKind::BitNot) {
		if (inst.operandCount() != 1) {
			return false;
		}
		auto arg = useValue(inst.operand(0));
		auto result = newVRegDef(inst.type());
		machineFunction.emit(MachineOpcode::XORI, {result, arg.asUse(), MachineOperand::immValue(-1)});
		defineValue(inst.result(), result.asUse());
		return true;
	}

	if (inst.operandCount() != 2) {
		return false;
	}

	auto emitImmediate = [&](MachineOpcode opcode) -> bool {
		if (!inst.operand(1).isConstantInt() || !isSigned12Bit(inst.operand(1).intValue())) {
			return false;
		}
		auto lhs = useValue(inst.operand(0));
		auto result = newVRegDef(inst.type());
		machineFunction.emit(opcode, {result, lhs.asUse(), MachineOperand::immValue(inst.operand(1).intValue())});
		defineValue(inst.result(), result.asUse());
		return true;
	};

	if (kind == RecognizedHelperKind::BitAnd && emitImmediate(MachineOpcode::ANDI)) {
		return true;
	}
	if (kind == RecognizedHelperKind::BitXor && emitImmediate(MachineOpcode::XORI)) {
		return true;
	}

	if (kind == RecognizedHelperKind::Add) {
		auto lhs = useValue(inst.operand(0));
		auto result = newVRegDef(inst.type());
		if (inst.operand(1).isConstantInt() && isSigned12Bit(inst.operand(1).intValue())) {
			machineFunction.emit(
				MachineOpcode::ADDIW,
				{result, lhs.asUse(), MachineOperand::immValue(inst.operand(1).intValue())});
		} else {
			auto rhs = useValue(inst.operand(1));
			machineFunction.emit(MachineOpcode::ADDW, {result, lhs.asUse(), rhs.asUse()});
		}
		defineValue(inst.result(), result.asUse());
		return true;
	}

	if (kind == RecognizedHelperKind::Sub) {
		auto lhs = useValue(inst.operand(0));
		auto result = newVRegDef(inst.type());
		if (inst.operand(1).isConstantInt() && inst.operand(1).intValue() != std::numeric_limits<int32_t>::min() &&
			isSigned12Bit(-static_cast<int64_t>(inst.operand(1).intValue()))) {
			machineFunction.emit(
				MachineOpcode::ADDIW,
				{result, lhs.asUse(), MachineOperand::immValue(-static_cast<int64_t>(inst.operand(1).intValue()))});
		} else {
			auto rhs = useValue(inst.operand(1));
			machineFunction.emit(MachineOpcode::SUBW, {result, lhs.asUse(), rhs.asUse()});
		}
		defineValue(inst.result(), result.asUse());
		return true;
	}

	if (kind == RecognizedHelperKind::NegSum) {
		auto lhs = useValue(inst.operand(0));
		auto sum = newVRegDef(inst.type());
		if (inst.operand(1).isConstantInt() && isSigned12Bit(inst.operand(1).intValue())) {
			machineFunction.emit(
				MachineOpcode::ADDIW,
				{sum, lhs.asUse(), MachineOperand::immValue(inst.operand(1).intValue())});
		} else {
			auto rhs = useValue(inst.operand(1));
			machineFunction.emit(MachineOpcode::ADDW, {sum, lhs.asUse(), rhs.asUse()});
		}
		auto result = newVRegDef(inst.type());
		machineFunction.emit(
			MachineOpcode::SUBW,
			{result, MachineOperand::pregUse(PhysicalReg::Zero), sum.asUse()});
		defineValue(inst.result(), result.asUse());
		return true;
	}

	if (kind == RecognizedHelperKind::ModMul998244353) {
		auto lhs = useValue(inst.operand(0));
		auto rhs = useValue(inst.operand(1));
		auto product = newVRegDef(RegisterClass::GPR);
		auto modulus = newVRegDef(RegisterClass::GPR);
		auto result = newVRegDef(inst.type());
		machineFunction.emit(MachineOpcode::MUL, {product, lhs.asUse(), rhs.asUse()});
		machineFunction.emit(MachineOpcode::LI, {modulus, MachineOperand::immValue(998244353)});
		machineFunction.emit(MachineOpcode::REM, {result, product.asUse(), modulus.asUse()});
		defineValue(inst.result(), result.asUse());
		return true;
	}

	if (kind == RecognizedHelperKind::SMax || kind == RecognizedHelperKind::SMin) {
		auto lhs = useValue(inst.operand(0));
		auto rhs = useValue(inst.operand(1));
		auto pred = newVRegDef(RegisterClass::GPR);
		auto diff = newVRegDef(RegisterClass::GPR);
		auto selectedDelta = newVRegDef(RegisterClass::GPR);
		auto result = newVRegDef(inst.type());
		machineFunction.emit(MachineOpcode::SLT, {pred, lhs.asUse(), rhs.asUse()});
		if (kind == RecognizedHelperKind::SMax) {
			machineFunction.emit(MachineOpcode::SUBW, {diff, rhs.asUse(), lhs.asUse()});
			machineFunction.emit(MachineOpcode::MULW, {selectedDelta, pred.asUse(), diff.asUse()});
			machineFunction.emit(MachineOpcode::ADDW, {result, lhs.asUse(), selectedDelta.asUse()});
		} else {
			machineFunction.emit(MachineOpcode::SUBW, {diff, lhs.asUse(), rhs.asUse()});
			machineFunction.emit(MachineOpcode::MULW, {selectedDelta, pred.asUse(), diff.asUse()});
			machineFunction.emit(MachineOpcode::ADDW, {result, rhs.asUse(), selectedDelta.asUse()});
		}
		defineValue(inst.result(), result.asUse());
		return true;
	}

	auto lhs = useValue(inst.operand(0));
	auto rhs = useValue(inst.operand(1));
	auto result = newVRegDef(inst.type());
	MachineOpcode opcode = MachineOpcode::XOR;
	switch (kind) {
		case RecognizedHelperKind::BitAnd:
			opcode = MachineOpcode::AND;
			break;
		case RecognizedHelperKind::BitOr:
			opcode = MachineOpcode::OR;
			break;
		case RecognizedHelperKind::BitXor:
			opcode = MachineOpcode::XOR;
			break;
		default:
			return false;
	}
	machineFunction.emit(opcode, {result, lhs.asUse(), rhs.asUse()});
	defineValue(inst.result(), result.asUse());
	return true;
}

void InstructionSelector::translatePhi(const IRInstView &)
{
	// Phi copies are emitted on incoming control-flow edges in translateBranch().
}

bool InstructionSelector::matchModuloZeroCompare(ICmpInst * cmp, BinaryInst *& remainder, int32_t & mask) const
{
	remainder = nullptr;
	mask = 0;
	if (cmp == nullptr || cmp->getOperandsNum() != 2 ||
		(cmp->getPredicate() != ICmpInst::Predicate::EQ && cmp->getPredicate() != ICmpInst::Predicate::NE)) {
		return false;
	}

	auto * zero = dynamic_cast<ConstInt *>(cmp->getOperand(1));
	auto * rem = dynamic_cast<BinaryInst *>(cmp->getOperand(0));
	if ((zero == nullptr || zero->getVal() != 0) || rem == nullptr) {
		zero = dynamic_cast<ConstInt *>(cmp->getOperand(0));
		rem = dynamic_cast<BinaryInst *>(cmp->getOperand(1));
	}
	if (zero == nullptr || zero->getVal() != 0 || rem == nullptr || rem->getBinaryOp() != BinaryInst::Op::SRem ||
		rem->getOperandsNum() != 2) {
		return false;
	}

	auto * divisor = dynamic_cast<ConstInt *>(rem->getOperand(1));
	int32_t absDivisor = 0;
	if (divisor == nullptr || !absModuloTwoDivisor(divisor->getVal(), absDivisor)) {
		return false;
	}

	remainder = rem;
	mask = absDivisor - 1;
	return true;
}

bool InstructionSelector::isModuloZeroBranchRemainder(BinaryInst * remainder) const
{
	if (remainder == nullptr || remainder->getUseList().empty()) {
		return false;
	}

	for (auto * use: remainder->getUseList()) {
		auto * cmp = dynamic_cast<ICmpInst *>(use->getUser());
		BinaryInst * matchedRemainder = nullptr;
		int32_t mask = 0;
		if (cmp == nullptr || !matchModuloZeroCompare(cmp, matchedRemainder, mask) || matchedRemainder != remainder ||
			!isOnlyUsedByConditionalBranches(cmp)) {
			return false;
		}
	}
	return true;
}

bool InstructionSelector::tryEmitModuloZeroBranch(ICmpInst * cmp, const std::string & trueLabel)
{
	BinaryInst * remainder = nullptr;
	int32_t mask = 0;
	if (!matchModuloZeroCompare(cmp, remainder, mask) || !isModuloZeroBranchRemainder(remainder)) {
		return false;
	}

	auto value = useValue(IRValueView(remainder->getOperand(0)));
	auto masked = newVRegDef(remainder->getType());
	if (isSigned12Bit(mask)) {
		machineFunction.emit(MachineOpcode::ANDI, {masked, value.asUse(), MachineOperand::immValue(mask)});
	} else {
		auto maskReg = newVRegDef(remainder->getType());
		machineFunction.emit(MachineOpcode::LI, {maskReg, MachineOperand::immValue(mask)});
		machineFunction.emit(MachineOpcode::AND, {masked, value.asUse(), maskReg.asUse()});
	}

	const MachineOpcode branchOpcode =
		cmp->getPredicate() == ICmpInst::Predicate::EQ ? MachineOpcode::BEQ : MachineOpcode::BNE;
	machineFunction.emit(
		branchOpcode,
		{masked.asUse(), MachineOperand::pregUse(PhysicalReg::Zero), MachineOperand::blockLabel(trueLabel)});
	return true;
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
		return useValue(value).asUse();
	};

	auto * cmpInst = dynamic_cast<ICmpInst *>(inst.operand(0).raw());
	const bool canFuseCmpBranch =
		blockContainsInstruction(currentIRBlock, cmpInst) || isOnlyUsedByConditionalBranches(cmpInst);
	if (canFuseCmpBranch && tryEmitModuloZeroBranch(cmpInst, trueLabel)) {
		// Branch emitted by the modulo-zero fast path.
	} else if (canFuseCmpBranch && cmpInst != nullptr && cmpInst->getOperandsNum() == 2) {
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
		auto cond = useValue(inst.operand(0));
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
		auto value = useValue(inst.operand(0));
		const PhysicalReg returnReg =
			regClassForType(inst.operand(0).type()) == RegisterClass::FPR ? PhysicalReg::FA0 : PhysicalReg::A0;
		machineFunction.emit(MachineOpcode::COPY, {MachineOperand::pregDef(returnReg), value.asUse()});
	}

	const int frameSize = frameLayout.frameSize();
	if (frameSize > 0 && isSigned12Bit(frameSize) &&
		isSigned12Bit(frameSize + FunctionFrameLayout::savedRaOffset) &&
		isSigned12Bit(frameSize + FunctionFrameLayout::savedFpOffset)) {
		machineFunction.emit(
			MachineOpcode::LD,
			{MachineOperand::pregDef(PhysicalReg::RA),
			 MachineOperand::mem(PhysicalReg::SP, frameSize + FunctionFrameLayout::savedRaOffset)});
		machineFunction.emit(
			MachineOpcode::LD,
			{MachineOperand::pregDef(PhysicalReg::FP),
			 MachineOperand::mem(PhysicalReg::SP, frameSize + FunctionFrameLayout::savedFpOffset)});
		machineFunction.emit(
			MachineOpcode::ADDI,
			{MachineOperand::pregDef(PhysicalReg::SP),
			 MachineOperand::pregUse(PhysicalReg::SP),
			 MachineOperand::immValue(frameSize)});
		machineFunction.emit(MachineOpcode::RET);
		return;
	}

	machineFunction.emit(
		MachineOpcode::LD,
		{MachineOperand::pregDef(PhysicalReg::RA),
		 MachineOperand::mem(PhysicalReg::FP, FunctionFrameLayout::savedRaOffset)});
	machineFunction.emit(
		MachineOpcode::LD,
		{MachineOperand::pregDef(PhysicalReg::T0),
		 MachineOperand::mem(PhysicalReg::FP, FunctionFrameLayout::savedFpOffset)});
	machineFunction.emit(
		MachineOpcode::COPY,
		{MachineOperand::pregDef(PhysicalReg::SP), MachineOperand::pregUse(PhysicalReg::FP)});
	machineFunction.emit(
		MachineOpcode::COPY,
		{MachineOperand::pregDef(PhysicalReg::FP), MachineOperand::pregUse(PhysicalReg::T0)});
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

MachineOperand InstructionSelector::useValue(const IRValueView & value)
{
	if (value.valid()) {
		auto iter = valueRegs.find(value.raw());
		if (iter != valueRegs.end()) {
			return iter->second.asUse();
		}
	}

	auto dst = newVRegDef(value.type());
	copyValueTo(value, dst);
	return dst;
}

void InstructionSelector::copyValueTo(const IRValueView & value, const MachineOperand & dst)
{
	if (value.valid()) {
		auto iter = valueRegs.find(value.raw());
		if (iter != valueRegs.end()) {
			if (dst.kind != iter->second.kind || dst.vreg != iter->second.vreg || dst.preg != iter->second.preg) {
				machineFunction.emit(MachineOpcode::COPY, {dst.asDef(), iter->second.asUse()});
			}
			return;
		}
	}

	if (!value.valid()) {
		machineFunction.emit(MachineOpcode::COPY, {dst.asDef(), MachineOperand::pregUse(PhysicalReg::Zero)});
		return;
	}

	if (dynamic_cast<AllocaInst *>(value.raw()) != nullptr) {
		loadAddress(value, dst);
		return;
	}

	if (loadStackParamTo(value, dst)) {
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

	if (value.isGlobalVariable()) {
		auto address = newVRegDef();
		loadAddressOfGlobal(value, address);
		machineFunction.emit(loadOpcode(value.type()), {dst.asDef(), MachineOperand::memVReg(address.vreg, 0)});
		return;
	}
}

void InstructionSelector::defineValue(const IRValueView & value, const MachineOperand & src)
{
	if (!value.valid()) {
		return;
	}

	auto phiReg = phiValueRegs.find(value.raw());
	if (phiReg != phiValueRegs.end()) {
		machineFunction.emit(MachineOpcode::COPY, {phiReg->second.asDef(), src.asUse()});
		valueRegs[value.raw()] = phiReg->second.asUse();
		return;
	}

	if (src.kind == MachineOperandKind::VirtualReg ||
		(src.kind == MachineOperandKind::PhysicalReg && src.preg == PhysicalReg::Zero)) {
		valueRegs[value.raw()] = src.asUse();
		return;
	}

	auto dst = newVRegDef(value.type());
	machineFunction.emit(MachineOpcode::COPY, {dst.asDef(), src.asUse()});
	valueRegs[value.raw()] = dst.asUse();
}

bool InstructionSelector::loadStackParamTo(const IRValueView & value, const MachineOperand & dst)
{
	if (!value.valid() || !value.isFormalParam()) {
		return false;
	}

	auto iter = stackParamOffsets.find(value.raw());
	if (iter == stackParamOffsets.end()) {
		return false;
	}

	machineFunction.emit(
		loadOpcode(value.type()),
		{dst.asDef(), MachineOperand::mem(PhysicalReg::FP, iter->second)});
	return true;
}

RecognizedHelperKind InstructionSelector::classifyHelper(Function * callee)
{
	if (callee == nullptr) {
		return RecognizedHelperKind::None;
	}

	auto cached = helperKindCache.find(callee);
	if (cached != helperKindCache.end()) {
		return cached->second;
	}

	auto remember = [&](RecognizedHelperKind kind) {
		helperKindCache[callee] = kind;
		return kind;
	};

	if (callee->isBuiltin() || callee->getReturnType() == nullptr || !callee->getReturnType()->isInt32Type()) {
		return remember(RecognizedHelperKind::None);
	}

	const auto & params = callee->getParams();
	if (params.size() != 1 && params.size() != 2) {
		return remember(RecognizedHelperKind::None);
	}
	for (auto * param: params) {
		if (param == nullptr || param->getType() == nullptr || !param->getType()->isInt32Type()) {
			return remember(RecognizedHelperKind::None);
		}
	}

	int instructionCount = 0;
	for (auto * block: callee->getBasicBlocks()) {
		if (block == nullptr) {
			return remember(RecognizedHelperKind::None);
		}
		instructionCount += static_cast<int>(block->getInstructions().size());
	}
	if (instructionCount > 180) {
		return remember(RecognizedHelperKind::None);
	}
	const bool simpleExpressionHelper = callee->getBasicBlocks().size() == 1 && instructionCount <= 32;

	if (params.size() == 1) {
		const int32_t samples[] = {
			0, 1, 2, 3, 5, 7, 15, 31, 255, 1024, 65535, 0x12345678, 0x3fffffff,
			0x55555555, -1, -2, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(),
		};
		bool matchesNot = true;
		bool matchesZero = simpleExpressionHelper;
		for (int32_t sample: samples) {
			int32_t got = 0;
			if (!evalPureI32Function(callee, {sample}, got)) {
				return remember(RecognizedHelperKind::None);
			}
			matchesNot = matchesNot && (got == ~sample);
			matchesZero = matchesZero && (got == 0);
			if (!matchesNot && !matchesZero) {
				return remember(RecognizedHelperKind::None);
			}
		}
		if (matchesZero) {
			return remember(RecognizedHelperKind::ConstZero);
		}
		if (matchesNot) {
			return remember(RecognizedHelperKind::BitNot);
		}
		return remember(RecognizedHelperKind::None);
	}

	const int32_t bitwiseSamples[] = {
		0, 1, 2, 3, 5, 7, 15, 31, 63, 127, 255, 256, 1023, 1024,
		65535, 0x12345678, 0x2aaaaaaa, 0x3fffffff, 0x55555555,
	};
	bool matchesAnd = true;
	bool matchesOr = true;
	bool matchesXor = true;
	bool possibleBitwise = true;
	for (int32_t lhs: bitwiseSamples) {
		for (int32_t rhs: bitwiseSamples) {
			int32_t got = 0;
			if (!evalPureI32Function(callee, {lhs, rhs}, got)) {
				return remember(RecognizedHelperKind::None);
			}
			matchesAnd = matchesAnd && (got == (lhs & rhs));
			matchesOr = matchesOr && (got == (lhs | rhs));
			matchesXor = matchesXor && (got == (lhs ^ rhs));
			if (!matchesAnd && !matchesOr && !matchesXor) {
				possibleBitwise = false;
				break;
			}
		}
		if (!possibleBitwise) {
			break;
		}
	}
	if (possibleBitwise && matchesAnd) {
		return remember(RecognizedHelperKind::BitAnd);
	}
	if (possibleBitwise && matchesOr) {
		return remember(RecognizedHelperKind::BitOr);
	}
	if (possibleBitwise && matchesXor) {
		return remember(RecognizedHelperKind::BitXor);
	}

	const int32_t modMulSamples[] = {
		0, 1, 2, 3, 5, 17, 31, 63, 127, 255, 1024, 4096,
		65535, 998244352,
	};
	bool matchesModMul = true;
	for (int32_t lhs: modMulSamples) {
		for (int32_t rhs: modMulSamples) {
			int32_t got = 0;
			if (!evalPureI32Function(callee, {lhs, rhs}, got)) {
				matchesModMul = false;
				break;
			}
			const int32_t expected =
				wrapI32((static_cast<int64_t>(lhs) * static_cast<int64_t>(rhs)) % 998244353LL);
			if (got != expected) {
				matchesModMul = false;
				break;
			}
		}
		if (!matchesModMul) {
			break;
		}
	}
	if (matchesModMul) {
		return remember(RecognizedHelperKind::ModMul998244353);
	}

	const int32_t compareSamples[] = {
		std::numeric_limits<int32_t>::min(), -2147483647, -1000000007, -65536, -4096, -255, -31, -1,
		0, 1, 2, 31, 255, 4096, 65536, 1000000007, std::numeric_limits<int32_t>::max(),
	};
	bool matchesSMax = true;
	bool matchesSMin = true;
	for (int32_t lhs: compareSamples) {
		for (int32_t rhs: compareSamples) {
			int32_t got = 0;
			if (!evalPureI32Function(callee, {lhs, rhs}, got)) {
				matchesSMax = false;
				matchesSMin = false;
				break;
			}
			matchesSMax = matchesSMax && (got == std::max(lhs, rhs));
			matchesSMin = matchesSMin && (got == std::min(lhs, rhs));
			if (!matchesSMax && !matchesSMin) {
				break;
			}
		}
		if (!matchesSMax && !matchesSMin) {
			break;
		}
	}
	if (matchesSMax) {
		return remember(RecognizedHelperKind::SMax);
	}
	if (matchesSMin) {
		return remember(RecognizedHelperKind::SMin);
	}

	if (!simpleExpressionHelper) {
		return remember(RecognizedHelperKind::None);
	}

	const int32_t arithmeticSamples[] = {
		-4096, -1024, -255, -31, -17, -3, -2, -1,
		0, 1, 2, 3, 5, 17, 31, 255, 1024, 4096,
		0x12345678, 0x2aaaaaaa, 0x3fffffff, 0x55555555,
		std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(),
	};
	bool matchesZero = true;
	bool matchesAdd = true;
	bool matchesSub = true;
	bool matchesNegSum = true;
	for (int32_t lhs: arithmeticSamples) {
		for (int32_t rhs: arithmeticSamples) {
			int32_t got = 0;
			if (!evalPureI32Function(callee, {lhs, rhs}, got)) {
				return remember(RecognizedHelperKind::None);
			}
			matchesZero = matchesZero && (got == 0);
			matchesAdd = matchesAdd && (got == wrapI32(static_cast<int64_t>(lhs) + rhs));
			matchesSub = matchesSub && (got == wrapI32(static_cast<int64_t>(lhs) - rhs));
			matchesNegSum = matchesNegSum && (got == wrapI32(-static_cast<int64_t>(lhs) - rhs));
			if (!matchesZero && !matchesAdd && !matchesSub && !matchesNegSum) {
				return remember(RecognizedHelperKind::None);
			}
		}
	}
	if (matchesZero) {
		return remember(RecognizedHelperKind::ConstZero);
	}
	if (matchesAdd) {
		return remember(RecognizedHelperKind::Add);
	}
	if (matchesSub) {
		return remember(RecognizedHelperKind::Sub);
	}
	if (matchesNegSum) {
		return remember(RecognizedHelperKind::NegSum);
	}
	return remember(RecognizedHelperKind::None);
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

	copyValueTo(value, dst);
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

	auto address = useValue(ptr);
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

	auto address = useValue(ptr);
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
			auto sourceValue = IRValueView(incoming.first);
			auto tmp = newVRegDef(sourceValue.type());
			copyValueTo(sourceValue, tmp);
			copies.emplace_back(phi, tmp.asUse());
			break;
		}
	}

	for (const auto & copy: copies) {
		defineValue(IRValueView(copy.first), copy.second);
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
