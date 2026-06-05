// Runtime memoization for bounded two-argument i32 self recursion.

#include "RuntimeMemoizePass.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ArrayType.h"
#include "BasicBlock.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CallInst.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalValue.h"
#include "GlobalVariable.h"
#include "ICmpInst.h"
#include "IntegerType.h"
#include "LoadInst.h"
#include "Module.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "StoreInst.h"
#include "Type.h"

namespace {

constexpr int32_t kFirstDim = 128;
constexpr int32_t kSecondDim = 2048;
constexpr int32_t kCapacity = kFirstDim * kSecondDim;

struct MemoGlobals {
	GlobalVariable * seen = nullptr;
	GlobalVariable * value = nullptr;
	GlobalVariable * epoch = nullptr;
};

std::string sanitizeName(std::string name)
{
	for (char & ch: name) {
		if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
			ch = '_';
		}
	}
	return name;
}

bool isI32(Type * type)
{
	return type != nullptr && type->isInt32Type();
}

bool isCandidate(Function * function)
{
	if (function == nullptr || function->isBuiltin() || !isI32(function->getReturnType())) {
		return false;
	}
	auto & params = function->getParams();
	if (params.size() != 2 || !isI32(params[0]->getType()) || !isI32(params[1]->getType())) {
		return false;
	}

	bool hasSelfCall = false;
	bool hasMultiSelfCallBlock = false;
	for (auto * block: function->getBasicBlocks()) {
		int32_t selfCallsInBlock = 0;
		for (auto * inst: block->getInstructions()) {
			if (dynamic_cast<StoreInst *>(inst) != nullptr) {
				return false;
			}
			auto * call = dynamic_cast<CallInst *>(inst);
			if (call == nullptr) {
				continue;
			}
			if (call->getCallee() != function) {
				return false;
			}
			hasSelfCall = true;
			++selfCallsInBlock;
		}
		if (selfCallsInBlock >= 2) {
			hasMultiSelfCallBlock = true;
		}
	}
	return hasSelfCall && hasMultiSelfCallBlock;
}

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

GetElementPtrInst * appendTableAddress(
	Module & module,
	BasicBlock * block,
	GlobalVariable * table,
	Value * index,
	const std::string & name)
{
	auto * zero = module.newConstInt(0);
	return appendInst(block, new GetElementPtrInst(block->getParent(), table, {zero, index}), name);
}

Value * appendIndex(Module & module, BasicBlock * block, Value * arg0, Value * arg1)
{
	auto * stride = module.newConstInt(kSecondDim);
	auto * row = appendInst(block, new BinaryInst(block->getParent(), BinaryInst::Op::Mul, arg0, stride), "memo.row");
	return appendInst(block, new BinaryInst(block->getParent(), BinaryInst::Op::Add, row, arg1), "memo.index");
}

ICmpInst * appendCompare(
	Module & module,
	BasicBlock * block,
	ICmpInst::Predicate pred,
	Value * lhs,
	int32_t rhs,
	const std::string & name)
{
	return appendInst(block, new ICmpInst(block->getParent(), pred, lhs, module.newConstInt(rhs)), name);
}

void replacePhiIncomingBlock(Function * function, BasicBlock * oldBlock, BasicBlock * newBlock)
{
	for (auto * block: function->getBasicBlocks()) {
		for (auto * inst: block->getInstructions()) {
			auto * phi = dynamic_cast<PhiInst *>(inst);
			if (phi == nullptr) {
				break;
			}
			phi->replaceIncomingBlock(oldBlock, newBlock);
		}
	}
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

std::vector<BasicBlock *> createRangeCheck(
	Module & module,
	Function * function,
	BasicBlock * start,
	BasicBlock * inRange,
	BasicBlock * outOfRange,
	const std::string & prefix)
{
	auto * arg0 = function->getParams()[0];
	auto * arg1 = function->getParams()[1];

	auto * arg0NonNeg = function->createBlock(prefix + ".arg0.nonneg");
	auto * arg0InRange = function->createBlock(prefix + ".arg0.range");
	auto * arg1NonNeg = function->createBlock(prefix + ".arg1.nonneg");

	auto * c0 = appendCompare(module, start, ICmpInst::Predicate::SGE, arg0, 0, "memo.arg0.ge0");
	appendInst(start, new BranchInst(function, c0, arg0NonNeg, outOfRange));

	auto * c1 = appendCompare(module, arg0NonNeg, ICmpInst::Predicate::SLT, arg0, kFirstDim, "memo.arg0.lt");
	appendInst(arg0NonNeg, new BranchInst(function, c1, arg0InRange, outOfRange));

	auto * c2 = appendCompare(module, arg0InRange, ICmpInst::Predicate::SGE, arg1, 0, "memo.arg1.ge0");
	appendInst(arg0InRange, new BranchInst(function, c2, arg1NonNeg, outOfRange));

	auto * c3 = appendCompare(module, arg1NonNeg, ICmpInst::Predicate::SLT, arg1, kSecondDim, "memo.arg1.lt");
	appendInst(arg1NonNeg, new BranchInst(function, c3, inRange, outOfRange));

	return {arg0NonNeg, arg0InRange, arg1NonNeg};
}

void createEntryCheck(Module & module, Function * function, const MemoGlobals & globals)
{
	auto * entry = function->getEntryBlock();
	if (entry == nullptr) {
		return;
	}

	auto * body = function->createBlock(entry->getIRName() + ".memo.body");
	body->getInstructions() = std::move(entry->getInstructions());
	entry->getInstructions().clear();
	replacePhiIncomingBlock(function, entry, body);

	auto * lookup = function->createBlock(entry->getIRName() + ".memo.lookup");
	auto * hit = function->createBlock(entry->getIRName() + ".memo.hit");
	auto checks = createRangeCheck(module, function, entry, lookup, body, entry->getIRName() + ".memo.check");

	Value * index = appendIndex(module, lookup, function->getParams()[0], function->getParams()[1]);
	auto * seenAddr = appendTableAddress(module, lookup, globals.seen, index, "memo.seen.addr");
	auto * seen = appendInst(lookup, new LoadInst(function, seenAddr), "memo.seen");
	auto * epoch = appendInst(lookup, new LoadInst(function, globals.epoch), "memo.epoch");
	auto * seenHit = appendInst(lookup, new ICmpInst(function, ICmpInst::Predicate::EQ, seen, epoch), "memo.hit");
	appendInst(lookup, new BranchInst(function, seenHit, hit, body));

	auto * valueAddr = appendTableAddress(module, hit, globals.value, index, "memo.value.addr");
	auto * cachedValue = appendInst(hit, new LoadInst(function, valueAddr), "memo.value");
	appendInst(hit, new ReturnInst(function, cachedValue));

	moveBlockAfter(function, checks[0], entry);
	moveBlockAfter(function, checks[1], checks[0]);
	moveBlockAfter(function, checks[2], checks[1]);
	moveBlockAfter(function, lookup, checks[2]);
	moveBlockAfter(function, hit, lookup);
	moveBlockAfter(function, body, hit);
}

void createStoreBlock(Module & module, Function * function, BasicBlock * block, Value * returnValue, const MemoGlobals & globals)
{
	auto * storeBlock = function->createBlock(block->getIRName() + ".memo.store");
	auto * finalBlock = function->createBlock(block->getIRName() + ".memo.return");
	auto checks = createRangeCheck(module, function, block, storeBlock, finalBlock, block->getIRName() + ".memo.retcheck");

	Value * index = appendIndex(module, storeBlock, function->getParams()[0], function->getParams()[1]);
	auto * valueAddr = appendTableAddress(module, storeBlock, globals.value, index, "memo.store.value.addr");
	appendInst(storeBlock, new StoreInst(function, returnValue, valueAddr));
	auto * seenAddr = appendTableAddress(module, storeBlock, globals.seen, index, "memo.store.seen.addr");
	auto * epoch = appendInst(storeBlock, new LoadInst(function, globals.epoch), "memo.store.epoch");
	appendInst(storeBlock, new StoreInst(function, epoch, seenAddr));
	appendInst(storeBlock, new BranchInst(function, finalBlock));

	appendInst(finalBlock, new ReturnInst(function, returnValue));

	moveBlockAfter(function, checks[0], block);
	moveBlockAfter(function, checks[1], checks[0]);
	moveBlockAfter(function, checks[2], checks[1]);
	moveBlockAfter(function, storeBlock, checks[2]);
	moveBlockAfter(function, finalBlock, storeBlock);
}

void rewriteReturns(Module & module, Function * function, const MemoGlobals & globals)
{
	std::vector<std::pair<BasicBlock *, Value *>> returns;
	for (auto * block: function->getBasicBlocks()) {
		auto & instructions = block->getInstructions();
		if (instructions.empty()) {
			continue;
		}
		auto * ret = dynamic_cast<ReturnInst *>(instructions.back());
		if (ret == nullptr || ret->getOperandsNum() != 1) {
			continue;
		}
		returns.push_back({block, ret->getOperand(0)});
	}

	for (auto [block, returnValue]: returns) {
		auto & instructions = block->getInstructions();
		auto * ret = instructions.back();
		instructions.pop_back();
		ret->clearOperands();
		ret->removeUses();
		delete ret;
		createStoreBlock(module, function, block, returnValue, globals);
	}
}

MemoGlobals createGlobals(Module & module, Function * function)
{
	const std::string suffix = sanitizeName(function->getName());
	auto * intType = IntegerType::getTypeInt();
	auto * tableType = new ArrayType(intType, kCapacity);

	MemoGlobals globals;
	globals.seen = module.newGlobalVariable(tableType, "__memo_seen_" + suffix, GlobalValue::InternalLinkage);
	globals.value = module.newGlobalVariable(tableType, "__memo_value_" + suffix, GlobalValue::InternalLinkage);
	globals.epoch = module.newGlobalVariable(intType, "__memo_epoch_" + suffix, GlobalValue::InternalLinkage);
	if (globals.epoch != nullptr) {
		globals.epoch->setInitializer(1);
	}
	return globals;
}

void insertEpochBumpBeforeCall(Module & module, BasicBlock * block, std::size_t callIndex, GlobalVariable * epochGlobal)
{
	auto & instructions = block->getInstructions();
	if (callIndex > instructions.size()) {
		return;
	}
	auto * function = block->getParent();
	std::vector<Instruction *> inserted;
	inserted.reserve(3);

	auto * epoch = new LoadInst(function, epochGlobal);
	assignName(function, epoch, "memo.bump.epoch");
	inserted.push_back(epoch);
	auto * next = new BinaryInst(function, BinaryInst::Op::Add, epoch, module.newConstInt(1));
	assignName(function, next, "memo.bump.next");
	inserted.push_back(next);
	inserted.push_back(new StoreInst(function, next, epochGlobal));

	instructions.insert(instructions.begin() + static_cast<std::ptrdiff_t>(callIndex), inserted.begin(), inserted.end());
}

void insertEpochBumps(Module & module, const std::vector<std::pair<Function *, MemoGlobals>> & memoized)
{
	if (memoized.empty()) {
		return;
	}

	std::unordered_set<Function *> candidates;
	std::unordered_map<Function *, GlobalVariable *> epochFor;
	for (const auto & item: memoized) {
		candidates.insert(item.first);
		epochFor[item.first] = item.second.epoch;
	}

	for (auto * function: module.getFunctionList()) {
		if (function == nullptr || function->isBuiltin()) {
			continue;
		}
		for (auto * block: function->getBasicBlocks()) {
			auto & instructions = block->getInstructions();
			for (std::size_t index = 0; index < instructions.size(); ++index) {
				auto * call = dynamic_cast<CallInst *>(instructions[index]);
				if (call == nullptr || candidates.find(call->getCallee()) == candidates.end()) {
					continue;
				}
				if (call->getCallee() == function) {
					continue;
				}
				insertEpochBumpBeforeCall(module, block, index, epochFor[call->getCallee()]);
				index += 3;
			}
		}
	}
}

} // namespace

bool RuntimeMemoizePass::run(Module & module)
{
	std::vector<Function *> candidates;
	for (auto * function: module.getFunctionList()) {
		if (isCandidate(function)) {
			candidates.push_back(function);
		}
	}

	std::vector<std::pair<Function *, MemoGlobals>> memoized;
	for (auto * function: candidates) {
		MemoGlobals globals = createGlobals(module, function);
		if (globals.seen == nullptr || globals.value == nullptr || globals.epoch == nullptr) {
			continue;
		}
		rewriteReturns(module, function, globals);
		createEntryCheck(module, function, globals);
		memoized.push_back({function, globals});
	}

	insertEpochBumps(module, memoized);
	return true;
}
