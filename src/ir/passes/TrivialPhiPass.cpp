// Eliminate phi nodes that always choose the same value.

#include "TrivialPhiPass.h"

#include "BasicBlock.h"
#include "ir/Instructions/PhiInst.h"
#include "ir/include/Function.h"
#include "ir/include/Instruction.h"
#include "ir/include/Module.h"
#include "ir/include/Value.h"

namespace {

Value * trivialReplacement(PhiInst * phi)
{
	if (phi == nullptr) {
		return nullptr;
	}

	Value * replacement = nullptr;
	for (const auto & incoming: phi->getIncomingValues()) {
		auto * value = incoming.first;
		if (value == phi) {
			continue;
		}
		if (replacement == nullptr) {
			replacement = value;
			continue;
		}
		if (replacement != value) {
			return nullptr;
		}
	}
	return replacement;
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
				auto * phi = dynamic_cast<PhiInst *>(*iter);
				if (phi == nullptr) {
					break;
				}

				auto * replacement = trivialReplacement(phi);
				if (replacement == nullptr) {
					++iter;
					continue;
				}

				// Remove the phi after redirecting its users to the single value.
				iter = instructions.erase(iter);
				phi->replaceAllUseWith(replacement);
				phi->clearOperands();
				phi->removeUses();
				delete phi;
				localChanged = true;
				changed = true;
			}
		}
	}

	return changed;
}

} // namespace

bool TrivialPhiPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
