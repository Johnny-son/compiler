// Convert return self-call patterns into parameter phi loops.

#include "TailCallOptPass.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "BasicBlock.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "FormalParam.h"
#include "Function.h"
#include "Instruction.h"
#include "Module.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "Use.h"
#include "User.h"

namespace {

struct TailCallSite {
	BasicBlock * block = nullptr;
	CallInst * call = nullptr;
	ReturnInst * ret = nullptr;
};

bool isPhi(Instruction * inst)
{
	return dynamic_cast<PhiInst *>(inst) != nullptr;
}

bool entryStartsWithPhi(Function * function)
{
	auto * entry = function != nullptr ? function->getEntryBlock() : nullptr;
	return entry != nullptr && !entry->getInstructions().empty() && isPhi(entry->getInstructions().front());
}

void moveBlockBefore(Function * function, BasicBlock * block, BasicBlock * before)
{
	auto & blocks = function->getBasicBlocks();
	auto blockIter = std::find(blocks.begin(), blocks.end(), block);
	auto beforeIter = std::find(blocks.begin(), blocks.end(), before);
	if (blockIter == blocks.end() || beforeIter == blocks.end() || blockIter == beforeIter) {
		return;
	}

	auto * value = *blockIter;
	blocks.erase(blockIter);
	beforeIter = std::find(blocks.begin(), blocks.end(), before);
	blocks.insert(beforeIter, value);
}

bool paramsUsedByExistingPhi(Function * function)
{
	for (auto * param: function->getParams()) {
		for (auto * use: param->getUseList()) {
			if (dynamic_cast<PhiInst *>(use->getUser()) != nullptr) {
				return true;
			}
		}
	}
	return false;
}

std::vector<TailCallSite> collectTailCalls(Function * function)
{
	std::vector<TailCallSite> sites;
	for (auto * block: function->getBasicBlocks()) {
		auto & instructions = block->getInstructions();
		if (instructions.size() < 2) {
			continue;
		}

		auto * ret = dynamic_cast<ReturnInst *>(instructions.back());
		auto * call = dynamic_cast<CallInst *>(instructions[instructions.size() - 2]);
		if (ret == nullptr || call == nullptr || call->getCallee() != function || ret->getOperandsNum() != 1 ||
		    ret->getOperand(0) != call || call->getUseList().size() != 1) {
			continue;
		}
		if (call->getOperandsNum() != static_cast<int32_t>(function->getParams().size())) {
			continue;
		}
		sites.push_back({block, call, ret});
	}
	return sites;
}

PhiInst * insertParamPhi(Function * function, BasicBlock * header, BasicBlock * preheader, FormalParam * param)
{
	auto * phi = new PhiInst(function, param->getType(), function->allocateLocalName(param->getName().empty() ? "tco.param" : param->getName() + ".tco"));
	phi->addIncoming(param, preheader);

	auto & instructions = header->getInstructions();
	auto insertPos = instructions.begin();
	while (insertPos != instructions.end() && isPhi(*insertPos)) {
		++insertPos;
	}
	instructions.insert(insertPos, phi);
	return phi;
}

bool replaceParamUse(Use * use, Value * replacement, const std::unordered_set<Use *> & skipped)
{
	if (use == nullptr || skipped.find(use) != skipped.end()) {
		return false;
	}

	auto * user = use->getUser();
	if (user == nullptr || dynamic_cast<PhiInst *>(user) != nullptr) {
		return false;
	}

	auto & operands = user->getOperands();
	for (std::size_t index = 0; index < operands.size(); ++index) {
		if (operands[index] == use) {
			user->setOperand(static_cast<int32_t>(index), replacement);
			return true;
		}
	}
	return false;
}

void replaceParamUses(Function * function, const std::vector<PhiInst *> & paramPhis, const std::unordered_set<Use *> & skipped)
{
	auto & params = function->getParams();
	for (std::size_t paramIndex = 0; paramIndex < params.size(); ++paramIndex) {
		std::vector<Use *> uses{params[paramIndex]->getUseList().begin(), params[paramIndex]->getUseList().end()};
		for (auto * use: uses) {
			(void) replaceParamUse(use, paramPhis[paramIndex], skipped);
		}
	}
}

void eraseTailCallAndReturn(TailCallSite site, Function * function, BasicBlock * header)
{
	auto & instructions = site.block->getInstructions();
	if (instructions.size() >= 2 && instructions[instructions.size() - 2] == site.call && instructions.back() == site.ret) {
		instructions.pop_back();
		instructions.pop_back();
	}

	site.ret->clearOperands();
	site.ret->removeUses();
	delete site.ret;

	site.call->clearOperands();
	site.call->removeUses();
	delete site.call;

	site.block->appendInst(new BranchInst(function, header));
}

bool runOnFunction(Function * function)
{
	if (function == nullptr || function->isBuiltin() || function->getReturnType() == nullptr ||
	    function->getReturnType()->isVoidType() || function->getParams().empty() || entryStartsWithPhi(function) ||
	    paramsUsedByExistingPhi(function)) {
		return false;
	}

	auto sites = collectTailCalls(function);
	if (sites.empty()) {
		return false;
	}

	auto * header = function->getEntryBlock();
	auto * preheader = function->createBlock(header->getIRName() + ".tco.entry");
	moveBlockBefore(function, preheader, header);
	preheader->appendInst(new BranchInst(function, header));

	std::vector<PhiInst *> paramPhis;
	std::unordered_set<Use *> skippedUses;
	for (auto * param: function->getParams()) {
		auto * phi = insertParamPhi(function, header, preheader, param);
		paramPhis.push_back(phi);
		for (auto * use: phi->getOperands()) {
			skippedUses.insert(use);
		}
	}

	replaceParamUses(function, paramPhis, skippedUses);

	for (auto site: sites) {
		for (std::size_t paramIndex = 0; paramIndex < paramPhis.size(); ++paramIndex) {
			paramPhis[paramIndex]->addIncoming(site.call->getOperand(static_cast<int32_t>(paramIndex)), site.block);
		}
		eraseTailCallAndReturn(site, function, header);
	}

	return true;
}

} // namespace

bool TailCallOptPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(function);
	}
	return true;
}
