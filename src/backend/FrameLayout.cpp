#include "backend/include/FrameLayout.h"

#include <algorithm>

#include "ir/include/Function.h"
#include "ir/include/Type.h"

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

StackObjectKind kindForValue(const IRValueView & value)
{
	if (value.isFormalParam()) {
		return StackObjectKind::FormalParam;
	}

	if (value.isLocalVariable()) {
		return StackObjectKind::LocalVariable;
	}

	if (value.isInstructionResult()) {
		return StackObjectKind::InstructionResult;
	}

	return StackObjectKind::LocalVariable;
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

	const int32_t size = slotSizeForType(value.type());
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

	for (const auto & local: function.locals()) {
		appendValueSlot(layout, local, cursor);
	}

	for (const auto & inst: function.instructions()) {
		if (inst.hasResult()) {
			appendValueSlot(layout, inst.result(), cursor);
		}
	}

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
