// Inline tiny pure functions so later IR passes can optimize through calls.

#include "InlineSimpleFunctionPass.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CallInst.h"
#include "CastInst.h"
#include "FCmpInst.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "ZExtInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "ReturnInst.h"

namespace {

constexpr int kMaxInlineInsts = 8;

using ValueMap = std::unordered_map<Value *, Value *>;

bool isInlineableInstruction(Instruction * inst)
{
	if (inst == nullptr || inst->isTerminator()) {
		return false;
	}
	return dynamic_cast<BinaryInst *>(inst) != nullptr || dynamic_cast<CastInst *>(inst) != nullptr ||
		   dynamic_cast<FCmpInst *>(inst) != nullptr || dynamic_cast<GetElementPtrInst *>(inst) != nullptr ||
		   dynamic_cast<ICmpInst *>(inst) != nullptr || dynamic_cast<ZExtInst *>(inst) != nullptr;
}

Value * mappedValue(Value * value, const ValueMap & valueMap)
{
	auto iter = valueMap.find(value);
	return iter == valueMap.end() ? value : iter->second;
}

Instruction * cloneInstruction(Function * caller, Instruction * inst, const ValueMap & valueMap)
{
	if (auto * binary = dynamic_cast<BinaryInst *>(inst); binary != nullptr && binary->getOperandsNum() == 2) {
		return new BinaryInst(
			caller,
			binary->getBinaryOp(),
			mappedValue(binary->getOperand(0), valueMap),
			mappedValue(binary->getOperand(1), valueMap));
	}
	if (auto * icmp = dynamic_cast<ICmpInst *>(inst); icmp != nullptr && icmp->getOperandsNum() == 2) {
		return new ICmpInst(
			caller,
			icmp->getPredicate(),
			mappedValue(icmp->getOperand(0), valueMap),
			mappedValue(icmp->getOperand(1), valueMap));
	}
	if (auto * fcmp = dynamic_cast<FCmpInst *>(inst); fcmp != nullptr && fcmp->getOperandsNum() == 2) {
		return new FCmpInst(
			caller,
			fcmp->getPredicate(),
			mappedValue(fcmp->getOperand(0), valueMap),
			mappedValue(fcmp->getOperand(1), valueMap));
	}
	if (auto * zext = dynamic_cast<ZExtInst *>(inst); zext != nullptr && zext->getOperandsNum() == 1) {
		return new ZExtInst(caller, mappedValue(zext->getSourceValue(), valueMap), zext->getType());
	}
	if (auto * cast = dynamic_cast<CastInst *>(inst); cast != nullptr && cast->getOperandsNum() == 1) {
		return new CastInst(caller, cast->getCastOp(), mappedValue(cast->getOperand(0), valueMap), cast->getType());
	}
	if (auto * gep = dynamic_cast<GetElementPtrInst *>(inst); gep != nullptr && gep->getOperandsNum() >= 1) {
		std::vector<Value *> indices;
		for (auto * index: gep->getIndices()) {
			indices.push_back(mappedValue(index, valueMap));
		}
		return new GetElementPtrInst(caller, mappedValue(gep->getBasePointer(), valueMap), indices);
	}
	return nullptr;
}

bool canInline(Function * callee)
{
	if (callee == nullptr || callee->isBuiltin() || callee->getReturnType()->isVoidType()) {
		return false;
	}
	if (callee->getBasicBlocks().size() != 1) {
		return false;
	}

	auto * block = callee->getBasicBlocks().front();
	const auto & instructions = block->getInstructions();
	if (instructions.empty()) {
		return false;
	}

	auto * ret = dynamic_cast<ReturnInst *>(instructions.back());
	if (ret == nullptr || ret->getOperandsNum() != 1) {
		return false;
	}

	int inlineInsts = 0;
	for (auto * inst: instructions) {
		if (inst == ret) {
			break;
		}
		if (!isInlineableInstruction(inst)) {
			return false;
		}
		++inlineInsts;
	}
	return inlineInsts <= kMaxInlineInsts;
}

bool inlineCall(Function * caller, CallInst * call, std::vector<Instruction *> & clonedInsts, Value *& replacement)
{
	if (caller == nullptr || call == nullptr) {
		return false;
	}

	Function * callee = call->getCallee();
	if (callee == nullptr || callee == caller || !canInline(callee)) {
		return false;
	}
	if (callee->getParams().size() != static_cast<std::size_t>(call->getOperandsNum())) {
		return false;
	}

	ValueMap valueMap;
	for (std::size_t index = 0; index < callee->getParams().size(); ++index) {
		valueMap[callee->getParams()[index]] = call->getOperand(static_cast<int32_t>(index));
	}

	auto * block = callee->getBasicBlocks().front();
	auto * ret = dynamic_cast<ReturnInst *>(block->getInstructions().back());
	for (auto * inst: block->getInstructions()) {
		if (inst == ret) {
			break;
		}

		auto * cloned = cloneInstruction(caller, inst, valueMap);
		if (cloned == nullptr) {
			return false;
		}
		clonedInsts.push_back(cloned);
		valueMap[inst] = cloned;
	}

	replacement = mappedValue(ret->getOperand(0), valueMap);
	return replacement != nullptr;
}

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	bool changed = false;
	for (auto * block: function->getBasicBlocks()) {
		auto & instructions = block->getInstructions();
		for (std::size_t index = 0; index < instructions.size(); ++index) {
			auto * call = dynamic_cast<CallInst *>(instructions[index]);
			if (call == nullptr || !call->hasResultValue()) {
				continue;
			}

			std::vector<Instruction *> clonedInsts;
			Value * replacement = nullptr;
			if (!inlineCall(function, call, clonedInsts, replacement)) {
				for (auto * cloned: clonedInsts) {
					cloned->clearOperands();
					cloned->removeUses();
					delete cloned;
				}
				continue;
			}

			call->replaceAllUseWith(replacement);
			auto insertPos = instructions.erase(instructions.begin() + static_cast<std::ptrdiff_t>(index));
			instructions.insert(insertPos, clonedInsts.begin(), clonedInsts.end());
			call->clearOperands();
			call->removeUses();
			delete call;
			index += clonedInsts.empty() ? 0 : clonedInsts.size() - 1;
			changed = true;
		}
	}
	return changed;
}

} // namespace

bool InlineSimpleFunctionPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
