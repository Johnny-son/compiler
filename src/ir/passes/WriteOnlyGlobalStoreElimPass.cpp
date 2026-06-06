#include "WriteOnlyGlobalStoreElimPass.h"

#include <algorithm>
#include <unordered_map>

#include "BasicBlock.h"
#include "CallInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "Module.h"
#include "ReturnInst.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

struct GlobalUseInfo {
	bool read = false;
	bool escaped = false;
};

GlobalVariable * rootGlobal(Value * value)
{
	if (auto * global = dynamic_cast<GlobalVariable *>(value); global != nullptr) {
		return global;
	}

	auto * gep = dynamic_cast<GetElementPtrInst *>(value);
	if (gep == nullptr) {
		return nullptr;
	}
	return rootGlobal(gep->getBasePointer());
}

void markEscapedOperands(Instruction * inst, std::unordered_map<GlobalVariable *, GlobalUseInfo> & info)
{
	if (inst == nullptr) {
		return;
	}

	for (int32_t index = 0; index < inst->getOperandsNum(); ++index) {
		auto * global = rootGlobal(inst->getOperand(index));
		if (global != nullptr) {
			info[global].escaped = true;
		}
	}
}

void collectUseInfo(Function * function, std::unordered_map<GlobalVariable *, GlobalUseInfo> & info)
{
	if (function == nullptr || function->isBuiltin()) {
		return;
	}

	for (auto * block: function->getBasicBlocks()) {
		if (block == nullptr) {
			continue;
		}

		for (auto * inst: block->getInstructions()) {
			if (inst == nullptr) {
				continue;
			}

			if (auto * load = dynamic_cast<LoadInst *>(inst); load != nullptr) {
				auto * global = rootGlobal(load->getPointerOperand());
				if (global != nullptr) {
					info[global].read = true;
				}
				continue;
			}

			if (auto * store = dynamic_cast<StoreInst *>(inst); store != nullptr) {
				auto * valueGlobal = rootGlobal(store->getValueOperand());
				if (valueGlobal != nullptr) {
					info[valueGlobal].escaped = true;
				}
				continue;
			}

			if (dynamic_cast<GetElementPtrInst *>(inst) != nullptr) {
				continue;
			}

			if (dynamic_cast<CallInst *>(inst) != nullptr || dynamic_cast<ReturnInst *>(inst) != nullptr) {
				markEscapedOperands(inst, info);
				continue;
			}

			// Be conservative for non-GEP pointer uses that this pass does not model.
			markEscapedOperands(inst, info);
		}
	}
}

bool canRemoveStore(StoreInst * store, const std::unordered_map<GlobalVariable *, GlobalUseInfo> & info)
{
	if (store == nullptr) {
		return false;
	}

	auto * global = rootGlobal(store->getPointerOperand());
	if (global == nullptr) {
		return false;
	}

	auto iter = info.find(global);
	if (iter == info.end()) {
		return true;
	}
	return !iter->second.read && !iter->second.escaped;
}

bool eraseStore(Function * function, StoreInst * store)
{
	if (function == nullptr || store == nullptr) {
		return false;
	}

	for (auto * block: function->getBasicBlocks()) {
		if (block == nullptr) {
			continue;
		}
		auto & instructions = block->getInstructions();
		auto iter = std::find(instructions.begin(), instructions.end(), store);
		if (iter == instructions.end()) {
			continue;
		}

		instructions.erase(iter);
		store->clearOperands();
		store->removeUses();
		delete store;
		return true;
	}
	return false;
}

bool removeDeadStores(Function * function, const std::unordered_map<GlobalVariable *, GlobalUseInfo> & info)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	std::vector<StoreInst *> storesToRemove;
	for (auto * block: function->getBasicBlocks()) {
		if (block == nullptr) {
			continue;
		}
		for (auto * inst: block->getInstructions()) {
			auto * store = dynamic_cast<StoreInst *>(inst);
			if (canRemoveStore(store, info)) {
				storesToRemove.push_back(store);
			}
		}
	}

	bool changed = false;
	for (auto * store: storesToRemove) {
		changed = eraseStore(function, store) || changed;
	}
	return changed;
}

} // namespace

bool WriteOnlyGlobalStoreElimPass::run(Module & module)
{
	std::unordered_map<GlobalVariable *, GlobalUseInfo> info;
	for (auto * global: module.getGlobalVariables()) {
		if (global != nullptr) {
			info.emplace(global, GlobalUseInfo{});
		}
	}

	for (auto * function: module.getFunctionList()) {
		collectUseInfo(function, info);
	}

	bool changed = false;
	for (auto * function: module.getFunctionList()) {
		changed = removeDeadStores(function, info) || changed;
	}
	return true;
}
