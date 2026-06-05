// Mark-and-sweep dead code elimination for pure IR subgraphs.

#include "ADCEPass.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "AllocaInst.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "CastInst.h"
#include "FCmpInst.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "StoreInst.h"
#include "ZExtInst.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"

namespace {

bool isPureInstruction(Instruction * inst)
{
	return dynamic_cast<AllocaInst *>(inst) != nullptr || dynamic_cast<BinaryInst *>(inst) != nullptr ||
		   dynamic_cast<CastInst *>(inst) != nullptr || dynamic_cast<FCmpInst *>(inst) != nullptr ||
		   dynamic_cast<GetElementPtrInst *>(inst) != nullptr || dynamic_cast<ICmpInst *>(inst) != nullptr ||
		   dynamic_cast<LoadInst *>(inst) != nullptr || dynamic_cast<PhiInst *>(inst) != nullptr ||
		   dynamic_cast<ZExtInst *>(inst) != nullptr;
}

bool isAlwaysLiveSeed(Instruction * inst)
{
	if (inst == nullptr) {
		return false;
	}
	return inst->isTerminator() || dynamic_cast<CallInst *>(inst) != nullptr || dynamic_cast<StoreInst *>(inst) != nullptr ||
		   dynamic_cast<ReturnInst *>(inst) != nullptr;
}

void markOperandInstructions(Instruction * inst, std::unordered_set<Instruction *> & live, std::vector<Instruction *> & worklist)
{
	if (inst == nullptr) {
		return;
	}

	for (int32_t index = 0; index < inst->getOperandsNum(); ++index) {
		auto * operandInst = dynamic_cast<Instruction *>(inst->getOperand(index));
		if (operandInst != nullptr && live.insert(operandInst).second) {
			worklist.push_back(operandInst);
		}
	}
}

bool eraseDeadPureInstructions(Function * function, const std::unordered_set<Instruction *> & live)
{
	bool changed = false;
	for (auto * block: function->getBasicBlocks()) {
		auto & instructions = block->getInstructions();
		for (auto iter = instructions.begin(); iter != instructions.end();) {
			auto * inst = *iter;
			if (inst == nullptr || !isPureInstruction(inst) || live.find(inst) != live.end()) {
				++iter;
				continue;
			}

			iter = instructions.erase(iter);
			inst->clearOperands();
			inst->removeUses();
			delete inst;
			changed = true;
		}
	}
	return changed;
}

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	std::unordered_set<Instruction *> live;
	std::vector<Instruction *> worklist;

	for (auto * block: function->getBasicBlocks()) {
		for (auto * inst: block->getInstructions()) {
			if (isAlwaysLiveSeed(inst) && live.insert(inst).second) {
				worklist.push_back(inst);
			}
		}
	}

	while (!worklist.empty()) {
		auto * inst = worklist.back();
		worklist.pop_back();
		markOperandInstructions(inst, live, worklist);
	}

	return eraseDeadPureInstructions(function, live);
}

} // namespace

bool ADCEPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
