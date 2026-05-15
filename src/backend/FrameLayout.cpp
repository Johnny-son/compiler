#include "FrameLayout.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Function.h"
#include "Type.h"
#include "AllocaInst.h"
#include "PhiInst.h"

namespace {

int alignTo(int value, int alignment)
{
	return (value + alignment - 1) / alignment * alignment;
}

int slotSizeForType(Type * type)
{
	if (type == nullptr) {
		return FunctionFrameLayout::stackSlotSize;
	}

	if (type->isPointerType()) {
		return 8;
	}

	const int size = type->getSize();
	if (size <= 0) {
		return FunctionFrameLayout::stackSlotSize;
	}

	return std::max(FunctionFrameLayout::stackSlotSize, size);
}

int slotSizeForValue(const IRValueView & value)
{
	auto * allocaInst = dynamic_cast<AllocaInst *>(value.raw());
	if (allocaInst != nullptr) {
		return slotSizeForType(allocaInst->getAllocatedType());
	}

	return slotSizeForType(value.type());
}

StackObjectKind kindForValue(const IRValueView & value)
{
	if (value.isFormalParam()) {
		return StackObjectKind::FormalParam;
	}

	if (dynamic_cast<AllocaInst *>(value.raw()) != nullptr) {
		return StackObjectKind::AllocaObject;
	}

	if (value.isInstructionResult()) {
		return StackObjectKind::InstructionResult;
	}

	return StackObjectKind::AllocaObject;
}

std::string debugNameForValue(const IRValueView & value)
{
	if (!value.name().empty()) {
		return value.name();
	}

	return value.irName();
}

void appendValueSlot(FunctionFrameLayout & layout, const IRValueView & value, int32_t & cursor)
{
	if (!value.valid() || layout.hasSlot(value.raw())) {
		return;
	}

	const int32_t size = slotSizeForValue(value);
	cursor += size;

	StackSlotInfo slot;
	slot.kind = kindForValue(value);
	slot.value = value.raw();
	slot.name = debugNameForValue(value);
	slot.offset = -cursor;
	slot.size = size;
	slot.align = FunctionFrameLayout::stackSlotSize;
	layout.addSlot(slot);
}

bool isReusableInstructionResult(const IRInstView & inst)
{
	return inst.hasResult() && dynamic_cast<AllocaInst *>(inst.raw()) == nullptr;
}

void rememberLastUse(
	std::unordered_map<Value *, std::size_t> & lastUse,
	const std::unordered_map<Value *, BasicBlock *> & defBlock,
	std::unordered_set<Value *> & escapingValues,
	const IRValueView & value,
	BasicBlock * useBlock,
	std::size_t instIndex)
{
	if (!value.isInstructionResult() || dynamic_cast<AllocaInst *>(value.raw()) != nullptr) {
		return;
	}
	auto defIter = defBlock.find(value.raw());
	if (defIter == defBlock.end() || defIter->second != useBlock) {
		escapingValues.insert(value.raw());
		return;
	}
	lastUse[value.raw()] = instIndex;
}

struct ReusableSlot {
	int32_t offset = 0;
	int32_t size = 0;
	std::size_t end = 0;
};

void appendReusableInstructionResultSlots(FunctionFrameLayout & layout, IRFunctionView function, int32_t & cursor)
{
	std::vector<std::pair<IRInstView, BasicBlock *>> insts;
	std::unordered_map<Value *, BasicBlock *> defBlock;
	std::unordered_map<BasicBlock *, std::size_t> blockEnd;
	std::unordered_map<Value *, std::size_t> lastUse;
	std::unordered_set<Value *> escapingValues;

	for (const auto & block: function.blocks()) {
		const auto blockInsts = block.instructions();
		for (const auto & inst: blockInsts) {
			if (isReusableInstructionResult(inst)) {
				defBlock.insert({inst.result().raw(), block.raw()});
			}
			insts.push_back({inst, block.raw()});
		}
		if (!blockInsts.empty()) {
			blockEnd[block.raw()] = insts.size() - 1;
		}
	}

	for (std::size_t index = 0; index < insts.size(); ++index) {
		const auto & inst = insts[index].first;
		BasicBlock * block = insts[index].second;
		for (const auto & operand: inst.operands()) {
			rememberLastUse(lastUse, defBlock, escapingValues, operand, block, index);
		}

		auto * phi = dynamic_cast<PhiInst *>(inst.raw());
		if (phi == nullptr) {
			continue;
		}
		for (const auto & incoming: phi->getIncomingValues()) {
			BasicBlock * predecessor = incoming.second;
			auto endIter = blockEnd.find(predecessor);
			const std::size_t edgeUseIndex = endIter != blockEnd.end() ? endIter->second : index;
			rememberLastUse(lastUse, defBlock, escapingValues, IRValueView(incoming.first), predecessor, edgeUseIndex);
		}
	}

	std::vector<ReusableSlot> reusableSlots;
	for (std::size_t index = 0; index < insts.size(); ++index) {
		const auto & inst = insts[index].first;
		if (!isReusableInstructionResult(inst)) {
			continue;
		}

		const IRValueView value = inst.result();
		const int32_t size = slotSizeForValue(value);
		std::size_t end = index;
		auto lastUseIter = lastUse.find(value.raw());
		if (lastUseIter != lastUse.end()) {
			end = lastUseIter->second;
		}

		int reusableIndex = -1;
		int32_t bestSize = 0;
		for (std::size_t slotIndex = 0; slotIndex < reusableSlots.size(); ++slotIndex) {
			const auto & candidate = reusableSlots[slotIndex];
			if (candidate.end >= index || candidate.size < size) {
				continue;
			}
			if (reusableIndex < 0 || candidate.size < bestSize) {
				reusableIndex = static_cast<int>(slotIndex);
				bestSize = candidate.size;
			}
		}

		StackSlotInfo slot;
		slot.kind = kindForValue(value);
		slot.value = value.raw();
		slot.name = debugNameForValue(value);
		slot.size = size;
		slot.align = FunctionFrameLayout::stackSlotSize;

		if (escapingValues.find(value.raw()) != escapingValues.end()) {
			cursor += size;
			slot.offset = -cursor;
		} else if (reusableIndex >= 0) {
			auto & reused = reusableSlots[static_cast<std::size_t>(reusableIndex)];
			reused.end = end;
			slot.offset = reused.offset;
		} else {
			cursor += size;
			slot.offset = -cursor;
			reusableSlots.push_back(ReusableSlot{slot.offset, size, end});
		}

		layout.addSlot(slot);
	}
}

} // namespace

FunctionFrameLayout::FunctionFrameLayout(IRFunctionView function) : func(function)
{}

bool FunctionFrameLayout::valid() const
{
	return func.valid();
}

IRFunctionView FunctionFrameLayout::function() const
{
	return func;
}

int32_t FunctionFrameLayout::frameSize() const
{
	return totalFrameSize;
}

int32_t FunctionFrameLayout::outgoingArgAreaSize() const
{
	return outgoingAreaSize;
}

const std::vector<StackSlotInfo> & FunctionFrameLayout::slots() const
{
	return slotInfos;
}

bool FunctionFrameLayout::hasSlot(Value * value) const
{
	return value != nullptr && slotIndex.find(value) != slotIndex.end();
}

const StackSlotInfo * FunctionFrameLayout::slotOf(Value * value) const
{
	auto iter = slotIndex.find(value);
	if (iter == slotIndex.end()) {
		return nullptr;
	}

	return &slotInfos[iter->second];
}

const StackSlotInfo * FunctionFrameLayout::spillSlot(int32_t id) const
{
	if (id < 0 || static_cast<std::size_t>(id) >= spillSlotIndices.size()) {
		return nullptr;
	}

	return &slotInfos[spillSlotIndices[static_cast<std::size_t>(id)]];
}

const StackSlotInfo * FunctionFrameLayout::returnAddressSlot() const
{
	for (const auto & slot: slotInfos) {
		if (slot.kind == StackObjectKind::SavedReturnAddress) {
			return &slot;
		}
	}

	return nullptr;
}

const StackSlotInfo * FunctionFrameLayout::oldFramePointerSlot() const
{
	for (const auto & slot: slotInfos) {
		if (slot.kind == StackObjectKind::SavedFramePointer) {
			return &slot;
		}
	}

	return nullptr;
}

void FunctionFrameLayout::setFrameSize(int32_t size)
{
	totalFrameSize = size;
}

void FunctionFrameLayout::setOutgoingArgAreaSize(int32_t size)
{
	outgoingAreaSize = size;
}

void FunctionFrameLayout::addSlot(const StackSlotInfo & slot)
{
	const std::size_t index = slotInfos.size();
	slotInfos.push_back(slot);
	if (slot.value != nullptr) {
		slotIndex.insert({slot.value, index});
	}
	if (slot.kind == StackObjectKind::SpillSlot) {
		spillSlotIndices.push_back(index);
	}
}

int32_t FunctionFrameLayout::createSpillSlot(int32_t size, int32_t align)
{
	const int32_t actualAlign = align > 0 ? align : stackSlotSize;
	const int32_t actualSize = alignTo(std::max(size, stackSlotSize), actualAlign);
	const int32_t cursor = alignTo(totalFrameSize + actualSize, stackAlign);

	StackSlotInfo slot;
	slot.kind = StackObjectKind::SpillSlot;
	slot.name = "spill." + std::to_string(spillSlotIndices.size());
	slot.offset = -cursor;
	slot.size = actualSize;
	slot.align = actualAlign;
	addSlot(slot);
	setFrameSize(cursor);

	return static_cast<int32_t>(spillSlotIndices.size() - 1);
}

FunctionFrameLayout FrameLayoutBuilder::build(IRFunctionView function)
{
	FunctionFrameLayout layout(function);
	if (!function.valid() || function.isBuiltin()) {
		return layout;
	}

	layout.addSlot(
		StackSlotInfo{StackObjectKind::SavedReturnAddress, nullptr, "saved_ra", FunctionFrameLayout::savedRaOffset, 8, 8});
	layout.addSlot(
		StackSlotInfo{StackObjectKind::SavedFramePointer, nullptr, "saved_fp", FunctionFrameLayout::savedFpOffset, 8, 8});

	int32_t cursor = FunctionFrameLayout::savedAreaSize;

	for (const auto & param: function.params()) {
		appendValueSlot(layout, param, cursor);
	}

	for (const auto & inst: function.instructions()) {
		if (dynamic_cast<AllocaInst *>(inst.raw()) != nullptr) {
			appendValueSlot(layout, inst.result(), cursor);
		}
	}

	appendReusableInstructionResultSlots(layout, function, cursor);

	const int32_t maxCallArgCount = function.raw()->getMaxFuncCallArgCnt();
	int32_t outgoingAreaSize = 0;
	if (maxCallArgCount > FunctionFrameLayout::argRegCount) {
		outgoingAreaSize = (maxCallArgCount - FunctionFrameLayout::argRegCount) * FunctionFrameLayout::stackSlotSize;
		outgoingAreaSize = alignTo(outgoingAreaSize, FunctionFrameLayout::stackSlotSize);

		cursor += outgoingAreaSize;
		layout.addSlot(StackSlotInfo{
			StackObjectKind::OutgoingArgArea,
			nullptr,
			"outgoing_arg_area",
			-cursor,
			outgoingAreaSize,
			FunctionFrameLayout::stackSlotSize});
	}

	layout.setOutgoingArgAreaSize(outgoingAreaSize);
	layout.setFrameSize(alignTo(cursor, FunctionFrameLayout::stackAlign));
	return layout;
}
