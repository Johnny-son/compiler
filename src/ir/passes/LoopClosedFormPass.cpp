// Conservative closed-form replacement for affine scalar recurrence loops.

#include "LoopClosedFormPass.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>

#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "ConstInt.h"
#include "Function.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "IRCFG.h"
#include "Instruction.h"
#include "LoadInst.h"
#include "LoopAnalysis.h"
#include "Module.h"
#include "PhiInst.h"
#include "StoreInst.h"
#include "Value.h"

namespace {

struct ClosedFormLoop {
	BasicBlock * preheader = nullptr;
	BasicBlock * header = nullptr;
	BasicBlock * body = nullptr;
	BasicBlock * exit = nullptr;
	PhiInst * induction = nullptr;
	Value * bound = nullptr;
	int32_t boundUpperExclusive = 0;
	GlobalVariable * global = nullptr;
	int32_t increment = 0;
	int32_t modulus = 0;
};

void assignName(Function * function, Instruction * inst, const std::string & hint)
{
	if (function != nullptr && inst != nullptr && inst->hasResultValue()) {
		inst->setIRName(function->allocateLocalName(hint));
	}
}

template<typename InstT>
InstT * appendInst(BasicBlock * block, InstT * inst, const std::string & name = "")
{
	if (block == nullptr || inst == nullptr) {
		delete inst;
		return nullptr;
	}
	assignName(block->getParent(), inst, name);
	block->getInstructions().push_back(inst);
	return inst;
}

bool isConstInt(Value * value, int32_t expected)
{
	auto * constant = dynamic_cast<ConstInt *>(value);
	return constant != nullptr && constant->getVal() == expected;
}

ConstInt * asConstInt(Value * value)
{
	return dynamic_cast<ConstInt *>(value);
}

bool containsBlock(const std::unordered_set<BasicBlock *> & blocks, BasicBlock * block)
{
	return block != nullptr && blocks.find(block) != blocks.end();
}

bool isDefinedInsideLoop(Value * value, const IRLoopInfo & loop, const IRCFG & cfg)
{
	auto * inst = dynamic_cast<Instruction *>(value);
	if (inst == nullptr) {
		return false;
	}
	auto iter = cfg.instructionBlock.find(inst);
	return iter != cfg.instructionBlock.end() && loop.contains(iter->second);
}

Value * phiValueFrom(PhiInst * phi, BasicBlock * block)
{
	if (phi == nullptr || block == nullptr) {
		return nullptr;
	}
	for (const auto & incoming: phi->getIncomingValues()) {
		if (incoming.second == block) {
			return incoming.first;
		}
	}
	return nullptr;
}

bool startsWithPhi(BasicBlock * block)
{
	return block != nullptr && !block->getInstructions().empty() &&
		   dynamic_cast<PhiInst *>(block->getInstructions().front()) != nullptr;
}

bool matchAddConst(BinaryInst * inst, Value * requiredValue, int32_t expectedConst)
{
	if (inst == nullptr || inst->getBinaryOp() != BinaryInst::Op::Add || inst->getOperandsNum() != 2) {
		return false;
	}
	return (inst->getOperand(0) == requiredValue && isConstInt(inst->getOperand(1), expectedConst)) ||
		   (inst->getOperand(1) == requiredValue && isConstInt(inst->getOperand(0), expectedConst));
}

bool matchAddLoadConst(BinaryInst * inst, GlobalVariable * global, int32_t & increment)
{
	if (inst == nullptr || inst->getBinaryOp() != BinaryInst::Op::Add || inst->getOperandsNum() != 2) {
		return false;
	}

	auto tryMatch = [&](Value * maybeLoad, Value * maybeConst) {
		auto * load = dynamic_cast<LoadInst *>(maybeLoad);
		auto * constant = asConstInt(maybeConst);
		if (load == nullptr || constant == nullptr || load->getPointerOperand() != global) {
			return false;
		}
		increment = constant->getVal();
		return increment > 0;
	};

	return tryMatch(inst->getOperand(0), inst->getOperand(1)) ||
		   tryMatch(inst->getOperand(1), inst->getOperand(0));
}

bool matchBoundedPositiveCount(Value * bound, int32_t & upperExclusive)
{
	auto * rem = dynamic_cast<BinaryInst *>(bound);
	if (rem == nullptr || rem->getBinaryOp() != BinaryInst::Op::SRem || rem->getOperandsNum() != 2) {
		return false;
	}

	auto * modulus = asConstInt(rem->getOperand(1));
	if (modulus == nullptr || modulus->getVal() <= 0) {
		return false;
	}
	upperExclusive = modulus->getVal();
	return true;
}

bool matchHeader(const IRLoopInfo & loop, const IRCFG & cfg, ClosedFormLoop & result)
{
	auto * header = loop.header;
	auto * branch = dynamic_cast<BranchInst *>(header != nullptr ? header->getTerminator() : nullptr);
	if (branch == nullptr || !branch->isConditional() || branch->getOperandsNum() != 1) {
		return false;
	}

	BasicBlock * trueTarget = branch->getTrueTarget();
	BasicBlock * falseTarget = branch->getFalseTarget();
	if (!loop.contains(trueTarget) || loop.contains(falseTarget)) {
		return false;
	}
	if (startsWithPhi(falseTarget)) {
		return false;
	}

	auto * cmp = dynamic_cast<ICmpInst *>(branch->getOperand(0));
	if (cmp == nullptr || cmp->getPredicate() != ICmpInst::Predicate::SLT || cmp->getOperandsNum() != 2) {
		return false;
	}

	for (auto * inst: header->getInstructions()) {
		auto * phi = dynamic_cast<PhiInst *>(inst);
		if (phi == nullptr) {
			break;
		}
		if (!isConstInt(phiValueFrom(phi, loop.preheader), 0)) {
			continue;
		}

		auto * next = dynamic_cast<BinaryInst *>(phiValueFrom(phi, loop.latches.front()));
		if (!matchAddConst(next, phi, 1)) {
			continue;
		}

		Value * bound = nullptr;
		if (cmp->getOperand(0) == phi) {
			bound = cmp->getOperand(1);
		} else {
			continue;
		}
		if (isDefinedInsideLoop(bound, loop, cfg)) {
			continue;
		}
		int32_t upperExclusive = 0;
		if (!matchBoundedPositiveCount(bound, upperExclusive)) {
			continue;
		}

		result.header = header;
		result.body = trueTarget;
		result.exit = falseTarget;
		result.induction = phi;
		result.bound = bound;
		result.boundUpperExclusive = upperExclusive;
		return true;
	}

	return false;
}

bool matchBody(const IRLoopInfo & loop, ClosedFormLoop & result)
{
	if (result.body == nullptr || result.body != loop.latches.front()) {
		return false;
	}

	auto * branch = dynamic_cast<BranchInst *>(result.body->getTerminator());
	if (branch == nullptr || branch->isConditional() || branch->getTarget() != result.header) {
		return false;
	}

	StoreInst * store = nullptr;
	BinaryInst * nextInduction = dynamic_cast<BinaryInst *>(phiValueFrom(result.induction, result.body));
	std::unordered_set<Instruction *> allowed;
	if (nextInduction != nullptr) {
		allowed.insert(nextInduction);
	}

	for (auto * inst: result.body->getInstructions()) {
		if (inst == branch || inst == nextInduction) {
			continue;
		}
		if (auto * candidateStore = dynamic_cast<StoreInst *>(inst); candidateStore != nullptr) {
			if (store != nullptr) {
				return false;
			}
			store = candidateStore;
			continue;
		}
		if (dynamic_cast<LoadInst *>(inst) != nullptr || dynamic_cast<BinaryInst *>(inst) != nullptr) {
			continue;
		}
		return false;
	}

	if (store == nullptr) {
		return false;
	}
	auto * global = dynamic_cast<GlobalVariable *>(store->getPointerOperand());
	auto * rem = dynamic_cast<BinaryInst *>(store->getValueOperand());
	if (global == nullptr || rem == nullptr || rem->getBinaryOp() != BinaryInst::Op::SRem ||
	    rem->getOperandsNum() != 2) {
		return false;
	}

	auto * modulus = asConstInt(rem->getOperand(1));
	auto * add = dynamic_cast<BinaryInst *>(rem->getOperand(0));
	int32_t increment = 0;
	if (modulus == nullptr || modulus->getVal() <= 0 || !matchAddLoadConst(add, global, increment)) {
		return false;
	}
	const int64_t maxBound = static_cast<int64_t>(result.boundUpperExclusive) - 1;
	const int64_t maxScaled = maxBound * static_cast<int64_t>(increment);
	const int64_t maxAdvanced = maxScaled + static_cast<int64_t>(modulus->getVal()) - 1;
	if (maxBound < 0 || maxScaled > std::numeric_limits<int32_t>::max() ||
	    maxAdvanced > std::numeric_limits<int32_t>::max()) {
		return false;
	}

	allowed.insert(store);
	allowed.insert(rem);
	allowed.insert(add);
	for (int32_t index = 0; index < add->getOperandsNum(); ++index) {
		if (auto * load = dynamic_cast<LoadInst *>(add->getOperand(index)); load != nullptr) {
			allowed.insert(load);
		}
	}

	for (auto * inst: result.body->getInstructions()) {
		if (inst == branch) {
			continue;
		}
		if (allowed.find(inst) == allowed.end()) {
			return false;
		}
	}

	result.global = global;
	result.increment = increment;
	result.modulus = modulus->getVal();
	return true;
}

bool matchLoop(Function * function, const IRLoopInfo & loop, ClosedFormLoop & result)
{
	if (function == nullptr || loop.preheader == nullptr || loop.header == nullptr || loop.latches.size() != 1 ||
	    loop.exitBlocks.size() != 1 || loop.blocks.size() != 2) {
		return false;
	}

	auto * preheaderBranch = dynamic_cast<BranchInst *>(loop.preheader->getTerminator());
	if (preheaderBranch == nullptr || preheaderBranch->isConditional() || preheaderBranch->getTarget() != loop.header) {
		return false;
	}

	IRCFG cfg = IRCFGBuilder::build(function);
	result = ClosedFormLoop{};
	result.preheader = loop.preheader;
	if (!matchHeader(loop, cfg, result)) {
		return false;
	}
	return matchBody(loop, result);
}

void moveBlockAfter(Function * function, BasicBlock * block, BasicBlock * after)
{
	auto & blocks = function->getBasicBlocks();
	auto blockIter = std::find(blocks.begin(), blocks.end(), block);
	auto afterIter = std::find(blocks.begin(), blocks.end(), after);
	if (blockIter == blocks.end() || afterIter == blocks.end() || blockIter == afterIter) {
		return;
	}
	auto * value = *blockIter;
	blocks.erase(blockIter);
	afterIter = std::find(blocks.begin(), blocks.end(), after);
	blocks.insert(afterIter + 1, value);
}

void replacePreheaderBranch(Module & module, ClosedFormLoop & loop)
{
	auto * function = loop.preheader->getParent();
	auto & instructions = loop.preheader->getInstructions();
	auto * oldBranch = instructions.empty() ? nullptr : dynamic_cast<BranchInst *>(instructions.back());
	if (oldBranch != nullptr) {
		instructions.pop_back();
		oldBranch->clearOperands();
		oldBranch->removeUses();
		delete oldBranch;
	}

	auto * fastBlock = function->createBlock(loop.header->getIRName() + ".closedform");
	moveBlockAfter(function, fastBlock, loop.preheader);

	auto * positive =
		appendInst(loop.preheader, new ICmpInst(function, ICmpInst::Predicate::SGT, loop.bound, module.newConstInt(0)),
		           "closedform.positive");
	appendInst(loop.preheader, new BranchInst(function, positive, fastBlock, loop.exit));

	auto * current = appendInst(fastBlock, new LoadInst(function, loop.global), "closedform.current");
	auto * base =
		appendInst(fastBlock, new BinaryInst(function, BinaryInst::Op::SRem, current, module.newConstInt(loop.modulus)),
		           "closedform.base");
	auto * scaled =
		appendInst(fastBlock, new BinaryInst(function, BinaryInst::Op::Mul, loop.bound, module.newConstInt(loop.increment)),
		           "closedform.scaled");
	auto * advanced =
		appendInst(fastBlock, new BinaryInst(function, BinaryInst::Op::Add, base, scaled), "closedform.advanced");
	auto * reduced =
		appendInst(fastBlock, new BinaryInst(function, BinaryInst::Op::SRem, advanced, module.newConstInt(loop.modulus)),
		           "closedform.reduced");
	appendInst(fastBlock, new StoreInst(function, reduced, loop.global));
	appendInst(fastBlock, new BranchInst(function, loop.exit));

	for (auto * inst: loop.header->getInstructions()) {
		auto * phi = dynamic_cast<PhiInst *>(inst);
		if (phi == nullptr) {
			break;
		}
		(void) phi->removeIncomingFrom(loop.preheader);
	}
}

bool runOnFunction(Module & module, Function * function)
{
	if (function == nullptr || function->isBuiltin()) {
		return false;
	}

	bool changed = false;
	bool localChanged = true;
	while (localChanged) {
		localChanged = false;
		for (const auto & loop: findNaturalLoops(function, true)) {
			ClosedFormLoop candidate;
			if (!matchLoop(function, loop, candidate)) {
				continue;
			}
			replacePreheaderBranch(module, candidate);
			localChanged = true;
			changed = true;
			break;
		}
	}
	return changed;
}

} // namespace

bool LoopClosedFormPass::run(Module & module)
{
	for (auto * function: module.getFunctionList()) {
		(void) runOnFunction(module, function);
	}
	return true;
}
