// Forward same-block loads from the latest exact-pointer store.

#include "LocalStoreForwardPass.h"

#include <unordered_map>

#include "BasicBlock.h"
#include "CallInst.h"
#include "Function.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "Module.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

using StoreMap = std::unordered_map<Value *, Value *>;

bool isMemoryBarrier(Instruction * inst)
{
	return dynamic_cast<StoreInst *>(inst) != nullptr || dynamic_cast<CallInst *>(inst) != nullptr;
}

bool runOnBlock(BasicBlock * block)
{
	if (block == nullptr) {
		return false;
	}

	bool changed = false;
	StoreMap availableStores;
	for (auto * inst: block->getInstructions()) {
		if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
			auto storeIter = availableStores.find(load->getPointerOperand());
			if (storeIter != availableStores.end()) {
				load->replaceAllUseWith(storeIter->second);
				changed = true;
			}
			continue;
		}

		if (auto * store = dynamic_cast<StoreInst *>(inst); store != nullptr) {
			availableStores.clear();
			availableStores[store->getPointerOperand()] = store->getValueOperand();
			continue;
		}

		if (isMemoryBarrier(inst)) {
			availableStores.clear();
		}
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

bool LocalStoreForwardPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
