// Eliminate repeated pure expressions in dominator-scoped regions.

#include "LocalCSEPass.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CastInst.h"
#include "ConstFloat.h"
#include "ConstInt.h"
#include "FCmpInst.h"
#include "GetElementPtrInst.h"
#include "IRCFG.h"
#include "ICmpInst.h"
#include "ZExtInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "Type.h"
#include "Value.h"

namespace {

std::string typeKey(Type * type)
{
	return std::to_string(reinterpret_cast<std::uintptr_t>(type));
}

struct GVNState {
	int nextNumber = 1;
	std::unordered_map<Value *, int> valueNumbers;
	std::unordered_map<std::string, int> expressionNumbers;
	std::unordered_map<int, Value *> representatives;

	int freshNumber(Value * representative)
	{
		const int number = nextNumber++;
		if (representative != nullptr) {
			representatives.emplace(number, representative);
		}
		return number;
	}

	void remember(Value * value, int number)
	{
		if (value == nullptr || number <= 0) {
			return;
		}
		valueNumbers[value] = number;
		representatives.try_emplace(number, value);
	}

	int stableNumber(const std::string & key, Value * representative)
	{
		auto iter = expressionNumbers.find(key);
		if (iter != expressionNumbers.end()) {
			if (representative != nullptr) {
				representatives.try_emplace(iter->second, representative);
			}
			return iter->second;
		}

		const int number = freshNumber(representative);
		expressionNumbers.emplace(key, number);
		return number;
	}

	int numberFor(Value * value)
	{
		if (value == nullptr) {
			return 0;
		}

		auto iter = valueNumbers.find(value);
		if (iter != valueNumbers.end()) {
			return iter->second;
		}

		if (auto * constant = dynamic_cast<ConstInt *>(value); constant != nullptr) {
			const int number = stableNumber(
				"const-int:" + typeKey(value->getType()) + ":" + std::to_string(constant->getVal()),
				value);
			remember(value, number);
			return number;
		}

		if (auto * constant = dynamic_cast<ConstFloat *>(value); constant != nullptr) {
			uint32_t bits = 0;
			const float floatValue = constant->getVal();
			std::memcpy(&bits, &floatValue, sizeof(bits));
			const int number = stableNumber(
				"const-float:" + typeKey(value->getType()) + ":" + std::to_string(bits),
				value);
			remember(value, number);
			return number;
		}

		const int number = freshNumber(value);
		remember(value, number);
		return number;
	}

	int numberForExpression(const std::string & key, Value * representative)
	{
		const int number = stableNumber("expr:" + key, representative);
		remember(representative, number);
		return number;
	}

	Value * representativeFor(int number) const
	{
		auto iter = representatives.find(number);
		return iter == representatives.end() ? nullptr : iter->second;
	}
};

bool isCommutativeBinary(BinaryInst::Op op)
{
	return op == BinaryInst::Op::Add || op == BinaryInst::Op::Mul;
}

bool isCommutativeICmp(ICmpInst::Predicate predicate)
{
	return predicate == ICmpInst::Predicate::EQ || predicate == ICmpInst::Predicate::NE;
}

std::pair<int, int> orderedNumbers(int lhs, int rhs)
{
	if (rhs < lhs) {
		return {rhs, lhs};
	}
	return {lhs, rhs};
}

std::string instructionKey(Instruction * inst, GVNState & state)
{
	if (inst == nullptr || inst->isTerminator() || !inst->hasResultValue()) {
		return "";
	}

	if (auto * binary = dynamic_cast<BinaryInst *>(inst); binary != nullptr && binary->getOperandsNum() == 2) {
		const int lhsNumber = state.numberFor(binary->getOperand(0));
		const int rhsNumber = state.numberFor(binary->getOperand(1));
		auto operands = isCommutativeBinary(binary->getBinaryOp())
			? orderedNumbers(lhsNumber, rhsNumber)
			: std::make_pair(lhsNumber, rhsNumber);
		return "bin:" + std::to_string(static_cast<int>(binary->getBinaryOp())) + ":" + typeKey(binary->getType()) +
			   ":" + std::to_string(operands.first) + ":" + std::to_string(operands.second);
	}
	if (auto * icmp = dynamic_cast<ICmpInst *>(inst); icmp != nullptr && icmp->getOperandsNum() == 2) {
		const int lhsNumber = state.numberFor(icmp->getOperand(0));
		const int rhsNumber = state.numberFor(icmp->getOperand(1));
		auto operands = isCommutativeICmp(icmp->getPredicate())
			? orderedNumbers(lhsNumber, rhsNumber)
			: std::make_pair(lhsNumber, rhsNumber);
		return "icmp:" + std::to_string(static_cast<int>(icmp->getPredicate())) +
			   ":" + std::to_string(operands.first) + ":" + std::to_string(operands.second);
	}
	if (auto * fcmp = dynamic_cast<FCmpInst *>(inst); fcmp != nullptr && fcmp->getOperandsNum() == 2) {
		return "fcmp:" + std::to_string(static_cast<int>(fcmp->getPredicate())) + ":" +
			   std::to_string(state.numberFor(fcmp->getOperand(0))) + ":" +
			   std::to_string(state.numberFor(fcmp->getOperand(1)));
	}
	if (auto * zext = dynamic_cast<ZExtInst *>(inst); zext != nullptr && zext->getOperandsNum() == 1) {
		return "zext:" + typeKey(zext->getType()) + ":" + std::to_string(state.numberFor(zext->getSourceValue()));
	}
	if (auto * cast = dynamic_cast<CastInst *>(inst); cast != nullptr && cast->getOperandsNum() == 1) {
		return "cast:" + std::to_string(static_cast<int>(cast->getCastOp())) + ":" + typeKey(cast->getType()) + ":" +
			   std::to_string(state.numberFor(cast->getOperand(0)));
	}
	if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst); gep != nullptr && gep->getOperandsNum() >= 1) {
		std::string key = "gep:" + typeKey(gep->getType()) + ":" + std::to_string(state.numberFor(gep->getBasePointer()));
		for (auto * index: gep->getIndices()) {
			key += ":" + std::to_string(state.numberFor(index));
		}
		return key;
	}

	return "";
}

bool runOnBlock(BasicBlock * block, const DominanceInfo & dominance, GVNState state)
{
	if (block == nullptr) {
		return false;
	}

	bool changed = false;
	auto & instructions = block->getInstructions();
	for (auto iter = instructions.begin(); iter != instructions.end();) {
		auto * inst = *iter;
		const std::string key = instructionKey(inst, state);
		if (key.empty()) {
			if (inst != nullptr && inst->hasResultValue()) {
				(void) state.numberFor(inst);
			}
			++iter;
			continue;
		}

		auto availableIter = state.expressionNumbers.find("expr:" + key);
		if (availableIter == state.expressionNumbers.end()) {
			(void) state.numberForExpression(key, inst);
			++iter;
			continue;
		}

		const int expressionNumber = availableIter->second;
		auto * replacement = state.representativeFor(expressionNumber);
		if (replacement == nullptr || replacement == inst) {
			state.remember(inst, expressionNumber);
			++iter;
			continue;
		}

		iter = instructions.erase(iter);
		inst->replaceAllUseWith(replacement);
		inst->clearOperands();
		inst->removeUses();
		delete inst;
		changed = true;
	}

	auto childrenIter = dominance.dominatorTreeChildren.find(block);
	if (childrenIter != dominance.dominatorTreeChildren.end()) {
		for (auto * child: childrenIter->second) {
			changed |= runOnBlock(child, dominance, state);
		}
	}

	return changed;
}

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	auto * entry = function->getEntryBlock();
	if (entry == nullptr) {
		return false;
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	DominanceInfo dominance = DominanceBuilder::build(cfg, entry);
	return runOnBlock(entry, dominance, GVNState{});
}

} // namespace

bool LocalCSEPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
