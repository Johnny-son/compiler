// Remove trivially dead IR instructions.

#include "DCEPass.h"

#include <algorithm>

#include "BasicBlock.h"
#include "AllocaInst.h"
#include "BinaryInst.h"
#include "CallInst.h"
#include "CastInst.h"
#include "FCmpInst.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "PhiInst.h"
#include "ZExtInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"

namespace {

bool hasSideEffect(Instruction * inst)
{
	if (inst == nullptr) {
		return true;
	}
	if (inst->isTerminator()) {
		return true;
	}
	if (dynamic_cast<CallInst *>(inst) != nullptr) {
		return true;
	}
	return false;
}

bool isKnownPureInstruction(Instruction * inst)
{
	return dynamic_cast<AllocaInst *>(inst) != nullptr || dynamic_cast<BinaryInst *>(inst) != nullptr ||
		   dynamic_cast<CastInst *>(inst) != nullptr || dynamic_cast<FCmpInst *>(inst) != nullptr ||
		   dynamic_cast<GetElementPtrInst *>(inst) != nullptr || dynamic_cast<ICmpInst *>(inst) != nullptr ||
		   dynamic_cast<LoadInst *>(inst) != nullptr || dynamic_cast<PhiInst *>(inst) != nullptr ||
		   dynamic_cast<ZExtInst *>(inst) != nullptr;
}

bool isDeadInstruction(Instruction * inst)
{
	return inst != nullptr && isKnownPureInstruction(inst) && !hasSideEffect(inst) && inst->getUseList().empty();
}

bool runOnFunction(Function * function)
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
				if (!isDeadInstruction(inst)) {
					++iter;
					continue;
				}

				iter = instructions.erase(iter);
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

bool DCEPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
