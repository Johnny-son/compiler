// Eliminate repeated pure expressions inside a single basic block.

#include "LocalCSEPass.h"

#include <cstdint>
#include <string>
#include <unordered_map>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "CastInst.h"
#include "FCmpInst.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "ZExtInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "Type.h"
#include "Value.h"

namespace {

std::string valueKey(Value * value)
{
	return std::to_string(reinterpret_cast<std::uintptr_t>(value));
}

std::string typeKey(Type * type)
{
	return std::to_string(reinterpret_cast<std::uintptr_t>(type));
}

std::string instructionKey(Instruction * inst)
{
	if (inst == nullptr || inst->isTerminator() || !inst->hasResultValue()) {
		return "";
	}

	if (auto * binary = dynamic_cast<BinaryInst *>(inst); binary != nullptr && binary->getOperandsNum() == 2) {
		return "bin:" + std::to_string(static_cast<int>(binary->getBinaryOp())) + ":" + typeKey(binary->getType()) +
			   ":" + valueKey(binary->getOperand(0)) + ":" + valueKey(binary->getOperand(1));
	}
	if (auto * icmp = dynamic_cast<ICmpInst *>(inst); icmp != nullptr && icmp->getOperandsNum() == 2) {
		return "icmp:" + std::to_string(static_cast<int>(icmp->getPredicate())) + ":" + valueKey(icmp->getOperand(0)) +
			   ":" + valueKey(icmp->getOperand(1));
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

bool runOnBlock(BasicBlock * block)
{
	if (block == nullptr) {
		return false;
	}

	bool changed = false;
	std::unordered_map<std::string, Instruction *> available;
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
	return changed;
}

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	bool changed = false;
	for (auto * block: function->getBasicBlocks()) {
		changed |= runOnBlock(block);
	}
	return changed;
}

} // namespace

bool LocalCSEPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
