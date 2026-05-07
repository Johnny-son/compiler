// Fold simple constant expressions in IR.

#include "ConstantFoldPass.h"

#include <algorithm>
#include <cstdint>

#include "BasicBlock.h"
#include "ir/Instructions/BinaryInst.h"
#include "ir/Instructions/FCmpInst.h"
#include "ir/Instructions/ICmpInst.h"
#include "ir/Instructions/ZExtInst.h"
#include "ir/Values/ConstFloat.h"
#include "ir/Values/ConstInt.h"
#include "ir/include/Function.h"
#include "ir/include/Instruction.h"
#include "ir/include/Module.h"
#include "ir/include/Type.h"
#include "ir/include/Value.h"

namespace {

int32_t wrapInt32(int64_t value)
{
	return static_cast<int32_t>(value);
}

Value * foldIntegerBinary(Module & module, BinaryInst * inst, ConstInt * lhs, ConstInt * rhs)
{
	const int64_t left = lhs->getVal();
	const int64_t right = rhs->getVal();

	switch (inst->getBinaryOp()) {
		case BinaryInst::Op::Add:
			return module.newConstInt(wrapInt32(left + right));
		case BinaryInst::Op::Sub:
			return module.newConstInt(wrapInt32(left - right));
		case BinaryInst::Op::Mul:
			return module.newConstInt(wrapInt32(left * right));
		case BinaryInst::Op::SDiv:
			return right == 0 ? nullptr : module.newConstInt(wrapInt32(left / right));
		case BinaryInst::Op::SRem:
			return right == 0 ? nullptr : module.newConstInt(wrapInt32(left % right));
		case BinaryInst::Op::FAdd:
		case BinaryInst::Op::FSub:
		case BinaryInst::Op::FMul:
		case BinaryInst::Op::FDiv:
			return nullptr;
	}
	return nullptr;
}

Value * foldFloatBinary(Module & module, BinaryInst * inst, ConstFloat * lhs, ConstFloat * rhs)
{
	const float left = lhs->getVal();
	const float right = rhs->getVal();

	switch (inst->getBinaryOp()) {
		case BinaryInst::Op::FAdd:
			return module.newConstFloat(left + right);
		case BinaryInst::Op::FSub:
			return module.newConstFloat(left - right);
		case BinaryInst::Op::FMul:
			return module.newConstFloat(left * right);
		case BinaryInst::Op::FDiv:
			return module.newConstFloat(left / right);
		case BinaryInst::Op::Add:
		case BinaryInst::Op::Sub:
		case BinaryInst::Op::Mul:
		case BinaryInst::Op::SDiv:
		case BinaryInst::Op::SRem:
			return nullptr;
	}
	return nullptr;
}

Value * foldIntegerCompare(Module & module, ICmpInst * inst, ConstInt * lhs, ConstInt * rhs)
{
	const int32_t left = lhs->getVal();
	const int32_t right = rhs->getVal();

	switch (inst->getPredicate()) {
		case ICmpInst::Predicate::EQ:
			return module.newConstBool(left == right);
		case ICmpInst::Predicate::NE:
			return module.newConstBool(left != right);
		case ICmpInst::Predicate::SLT:
			return module.newConstBool(left < right);
		case ICmpInst::Predicate::SLE:
			return module.newConstBool(left <= right);
		case ICmpInst::Predicate::SGT:
			return module.newConstBool(left > right);
		case ICmpInst::Predicate::SGE:
			return module.newConstBool(left >= right);
	}
	return nullptr;
}

Value * foldFloatCompare(Module & module, FCmpInst * inst, ConstFloat * lhs, ConstFloat * rhs)
{
	const float left = lhs->getVal();
	const float right = rhs->getVal();

	switch (inst->getPredicate()) {
		case FCmpInst::Predicate::OEQ:
			return module.newConstBool(left == right);
		case FCmpInst::Predicate::ONE:
			return module.newConstBool(left != right);
		case FCmpInst::Predicate::OLT:
			return module.newConstBool(left < right);
		case FCmpInst::Predicate::OLE:
			return module.newConstBool(left <= right);
		case FCmpInst::Predicate::OGT:
			return module.newConstBool(left > right);
		case FCmpInst::Predicate::OGE:
			return module.newConstBool(left >= right);
	}
	return nullptr;
}

Value * foldZExt(Module & module, ZExtInst * inst, ConstInt * source)
{
	if (inst == nullptr || source == nullptr || inst->getType() == nullptr) {
		return nullptr;
	}

	if (inst->getType()->isInt1Byte()) {
		return module.newConstBool(source->getVal() != 0);
	}
	if (inst->getType()->isInt32Type()) {
		return module.newConstInt(source->getVal() != 0 ? 1 : 0);
	}
	return nullptr;
}

Value * foldInstruction(Module & module, Instruction * inst)
{
	auto * binary = dynamic_cast<BinaryInst *>(inst);
	if (binary != nullptr && binary->getOperandsNum() == 2) {
		auto * lhs = binary->getOperand(0);
		auto * rhs = binary->getOperand(1);
		if (auto * lhsInt = dynamic_cast<ConstInt *>(lhs); lhsInt != nullptr) {
			auto * rhsInt = dynamic_cast<ConstInt *>(rhs);
			return rhsInt == nullptr ? nullptr : foldIntegerBinary(module, binary, lhsInt, rhsInt);
		}
		if (auto * lhsFloat = dynamic_cast<ConstFloat *>(lhs); lhsFloat != nullptr) {
			auto * rhsFloat = dynamic_cast<ConstFloat *>(rhs);
			return rhsFloat == nullptr ? nullptr : foldFloatBinary(module, binary, lhsFloat, rhsFloat);
		}
	}

	auto * icmp = dynamic_cast<ICmpInst *>(inst);
	if (icmp != nullptr && icmp->getOperandsNum() == 2) {
		auto * lhsInt = dynamic_cast<ConstInt *>(icmp->getOperand(0));
		auto * rhsInt = dynamic_cast<ConstInt *>(icmp->getOperand(1));
		return lhsInt == nullptr || rhsInt == nullptr ? nullptr : foldIntegerCompare(module, icmp, lhsInt, rhsInt);
	}

	auto * fcmp = dynamic_cast<FCmpInst *>(inst);
	if (fcmp != nullptr && fcmp->getOperandsNum() == 2) {
		auto * lhsFloat = dynamic_cast<ConstFloat *>(fcmp->getOperand(0));
		auto * rhsFloat = dynamic_cast<ConstFloat *>(fcmp->getOperand(1));
		return lhsFloat == nullptr || rhsFloat == nullptr ? nullptr : foldFloatCompare(module, fcmp, lhsFloat, rhsFloat);
	}

	auto * zext = dynamic_cast<ZExtInst *>(inst);
	if (zext != nullptr && zext->getOperandsNum() == 1) {
		auto * source = dynamic_cast<ConstInt *>(zext->getSourceValue());
		return source == nullptr ? nullptr : foldZExt(module, zext, source);
	}

	return nullptr;
}

bool runOnFunction(Module & module, Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	bool changed = false;
	bool localChanged = true;
	while (localChanged) {
		localChanged = false;
		for (auto * block: function->getBasicBlocks()) {
			auto & instructions = block->getInstructions();
			for (auto iter = instructions.begin(); iter != instructions.end();) {
				auto * inst = *iter;
				auto * foldedValue = foldInstruction(module, inst);
				if (foldedValue == nullptr) {
					++iter;
					continue;
				}

				iter = instructions.erase(iter);
				inst->replaceAllUseWith(foldedValue);
				inst->clearOperands();
				inst->removeUses();
				delete inst;
				localChanged = true;
				changed = true;
			}
		}
	}

	return changed;
}

} // namespace

bool ConstantFoldPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(module, function);
	}
	return true;
}
