// Fold simple constant expressions in IR.

#include "ConstantFoldPass.h"

#include <algorithm>
#include <cstdint>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "FCmpInst.h"
#include "ICmpInst.h"
#include "ZExtInst.h"
#include "ConstFloat.h"
#include "ConstInt.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "Type.h"
#include "Value.h"

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

bool isConstIntValue(Value * value, int32_t expected)
{
	auto * constInt = dynamic_cast<ConstInt *>(value);
	return constInt != nullptr && constInt->getVal() == expected;
}

Value * foldIntegerIdentity(Module & module, BinaryInst * inst, Value * lhs, Value * rhs)
{
	switch (inst->getBinaryOp()) {
		case BinaryInst::Op::Add:
			if (isConstIntValue(lhs, 0)) {
				return rhs;
			}
			if (isConstIntValue(rhs, 0)) {
				return lhs;
			}
			break;
		case BinaryInst::Op::Sub:
			if (isConstIntValue(rhs, 0)) {
				return lhs;
			}
			if (lhs == rhs) {
				return module.newConstInt(0);
			}
			break;
		case BinaryInst::Op::Mul:
			if (isConstIntValue(lhs, 0) || isConstIntValue(rhs, 0)) {
				return module.newConstInt(0);
			}
			if (isConstIntValue(lhs, 1)) {
				return rhs;
			}
			if (isConstIntValue(rhs, 1)) {
				return lhs;
			}
			break;
		case BinaryInst::Op::SDiv:
			if (isConstIntValue(rhs, 1)) {
				return lhs;
			}
			break;
		case BinaryInst::Op::SRem:
			if (isConstIntValue(rhs, 1)) {
				return module.newConstInt(0);
			}
			break;
		case BinaryInst::Op::FAdd:
		case BinaryInst::Op::FSub:
		case BinaryInst::Op::FMul:
		case BinaryInst::Op::FDiv:
			break;
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

Value * foldIntegerCompareIdentity(Module & module, ICmpInst * inst, Value * lhs, Value * rhs)
{
	if (lhs != rhs) {
		return nullptr;
	}

	switch (inst->getPredicate()) {
		case ICmpInst::Predicate::EQ:
		case ICmpInst::Predicate::SLE:
		case ICmpInst::Predicate::SGE:
			return module.newConstBool(true);
		case ICmpInst::Predicate::NE:
		case ICmpInst::Predicate::SLT:
		case ICmpInst::Predicate::SGT:
			return module.newConstBool(false);
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
		auto * lhsInt = dynamic_cast<ConstInt *>(lhs);
		auto * rhsInt = dynamic_cast<ConstInt *>(rhs);
		if (lhsInt != nullptr && rhsInt != nullptr) {
			return foldIntegerBinary(module, binary, lhsInt, rhsInt);
		}

		if (auto * replacement = foldIntegerIdentity(module, binary, lhs, rhs); replacement != nullptr) {
			return replacement;
		}

		auto * lhsFloat = dynamic_cast<ConstFloat *>(lhs);
		auto * rhsFloat = dynamic_cast<ConstFloat *>(rhs);
		if (lhsFloat != nullptr && rhsFloat != nullptr) {
			return foldFloatBinary(module, binary, lhsFloat, rhsFloat);
		}
	}

	auto * icmp = dynamic_cast<ICmpInst *>(inst);
	if (icmp != nullptr && icmp->getOperandsNum() == 2) {
		auto * lhs = icmp->getOperand(0);
		auto * rhs = icmp->getOperand(1);
		auto * lhsInt = dynamic_cast<ConstInt *>(lhs);
		auto * rhsInt = dynamic_cast<ConstInt *>(rhs);
		if (lhsInt != nullptr && rhsInt != nullptr) {
			return foldIntegerCompare(module, icmp, lhsInt, rhsInt);
		}
		return foldIntegerCompareIdentity(module, icmp, lhs, rhs);
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
