#include "FrameLayout.h"

#include <algorithm>

#include "Function.h"
#include "Type.h"
#include "AllocaInst.h"

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
	const int32_t align = FunctionFrameLayout::stackSlotSize;
	cursor = alignTo(cursor + size, align);

	StackSlotInfo slot;
	slot.kind = kindForValue(value);
	slot.value = value.raw();
	slot.name = debugNameForValue(value);
	slot.offset = -cursor;
	slot.size = size;
	slot.align = align;
	layout.addSlot(slot);
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

	for (const auto & inst: function.instructions()) {
		if (dynamic_cast<AllocaInst *>(inst.raw()) != nullptr) {
			appendValueSlot(layout, inst.result(), cursor);
		}
	}

	const int32_t maxCallArgCount = function.raw()->getMaxFuncCallArgCnt();
	int32_t outgoingAreaSize = 0;
	if (maxCallArgCount > FunctionFrameLayout::argRegCount) {
		outgoingAreaSize = (maxCallArgCount - FunctionFrameLayout::argRegCount) * FunctionFrameLayout::stackSlotSize;
		outgoingAreaSize = alignTo(outgoingAreaSize, FunctionFrameLayout::stackSlotSize);

		cursor = alignTo(cursor + outgoingAreaSize, FunctionFrameLayout::stackSlotSize);
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
