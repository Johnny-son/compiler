// Eliminate repeated pure expressions in dominator-scoped regions.

#include "LocalCSEPass.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CastInst.h"
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

using AvailableMap = std::unordered_map<std::string, Instruction *>;

std::string valueKey(Value * value)
{
	return std::to_string(reinterpret_cast<std::uintptr_t>(value));
}

std::string typeKey(Type * type)
{
	return std::to_string(reinterpret_cast<std::uintptr_t>(type));
}

bool isCommutativeBinary(BinaryInst::Op op)
{
	return op == BinaryInst::Op::Add || op == BinaryInst::Op::Mul;
}

bool isCommutativeICmp(ICmpInst::Predicate predicate)
{
	return predicate == ICmpInst::Predicate::EQ || predicate == ICmpInst::Predicate::NE;
}

std::pair<Value *, Value *> orderedOperands(Value * lhs, Value * rhs)
{
	if (valueKey(rhs) < valueKey(lhs)) {
		return {rhs, lhs};
	}
	return {lhs, rhs};
}

std::string instructionKey(Instruction * inst)
{
	if (inst == nullptr || inst->isTerminator() || !inst->hasResultValue()) {
		return "";
	}

	if (auto * binary = dynamic_cast<BinaryInst *>(inst); binary != nullptr && binary->getOperandsNum() == 2) {
		auto operands = isCommutativeBinary(binary->getBinaryOp())
			? orderedOperands(binary->getOperand(0), binary->getOperand(1))
			: std::make_pair(binary->getOperand(0), binary->getOperand(1));
		return "bin:" + std::to_string(static_cast<int>(binary->getBinaryOp())) + ":" + typeKey(binary->getType()) +
			   ":" + valueKey(operands.first) + ":" + valueKey(operands.second);
	}
	if (auto * icmp = dynamic_cast<ICmpInst *>(inst); icmp != nullptr && icmp->getOperandsNum() == 2) {
		auto operands = isCommutativeICmp(icmp->getPredicate())
			? orderedOperands(icmp->getOperand(0), icmp->getOperand(1))
			: std::make_pair(icmp->getOperand(0), icmp->getOperand(1));
		return "icmp:" + std::to_string(static_cast<int>(icmp->getPredicate())) +
			   ":" + valueKey(operands.first) + ":" + valueKey(operands.second);
	}
	if (auto * fcmp = dynamic_cast<FCmpInst *>(inst); fcmp != nullptr && fcmp->getOperandsNum() == 2) {
		return "fcmp:" + std::to_string(static_cast<int>(fcmp->getPredicate())) + ":" + valueKey(fcmp->getOperand(0)) +
			   ":" + valueKey(fcmp->getOperand(1));
	}
	if (auto * zext = dynamic_cast<ZExtInst *>(inst); zext != nullptr && zext->getOperandsNum() == 1) {
		return "zext:" + typeKey(zext->getType()) + ":" + valueKey(zext->getSourceValue());
	}
	if (auto * cast = dynamic_cast<CastInst *>(inst); cast != nullptr && cast->getOperandsNum() == 1) {
		return "cast:" + std::to_string(static_cast<int>(cast->getCastOp())) + ":" + typeKey(cast->getType()) + ":" +
			   valueKey(cast->getOperand(0));
	}
	if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst); gep != nullptr && gep->getOperandsNum() >= 1) {
		std::string key = "gep:" + typeKey(gep->getType()) + ":" + valueKey(gep->getBasePointer());
		for (auto * index: gep->getIndices()) {
			key += ":" + valueKey(index);
		}
		return key;
	}

	return "";
}

bool runOnBlock(BasicBlock * block, const DominanceInfo & dominance, AvailableMap & available)
{
	if (block == nullptr) {
		return false;
	}

	bool changed = false;
	std::vector<std::pair<std::string, Instruction *>> inserted;
	auto & instructions = block->getInstructions();
	for (auto iter = instructions.begin(); iter != instructions.end();) {
		auto * inst = *iter;
		const std::string key = instructionKey(inst);
		if (key.empty()) {
			++iter;
			continue;
		}

		auto availableIter = available.find(key);
		if (availableIter == available.end()) {
			available.insert({key, inst});
			inserted.push_back({key, inst});
			++iter;
			continue;
		}

		auto * replacement = availableIter->second;
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
			changed |= runOnBlock(child, dominance, available);
		}
	}

	for (auto iter = inserted.rbegin(); iter != inserted.rend(); ++iter) {
		auto availableIter = available.find(iter->first);
		if (availableIter != available.end() && availableIter->second == iter->second) {
			available.erase(availableIter);
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
	AvailableMap available;
	return runOnBlock(entry, dominance, available);
}

} // namespace

bool LocalCSEPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
