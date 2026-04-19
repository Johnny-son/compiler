#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/include/IRAdapter.h"

enum class StackObjectKind : std::int8_t {
	SavedReturnAddress,
	SavedFramePointer,
	FormalParam,
	LocalVariable,
	InstructionResult,
	OutgoingArgArea
};

struct StackSlotInfo {
	StackObjectKind kind = StackObjectKind::LocalVariable;
	Value * value = nullptr;
	std::string name;
	int32_t offset = 0;
	int32_t size = 0;
	int32_t align = 0;
};

class FunctionFrameLayout {

public:
	static constexpr int stackSlotSize = 8;
	static constexpr int stackAlign = 16;
	static constexpr int argRegCount = 8;
	static constexpr int savedAreaSize = 16;
	static constexpr int savedRaOffset = -8;
	static constexpr int savedFpOffset = -16;

	explicit FunctionFrameLayout(IRFunctionView function = IRFunctionView());

	[[nodiscard]] bool valid() const;
	[[nodiscard]] IRFunctionView function() const;
	[[nodiscard]] int32_t frameSize() const;
	[[nodiscard]] int32_t outgoingArgAreaSize() const;
	[[nodiscard]] const std::vector<StackSlotInfo> & slots() const;
	[[nodiscard]] bool hasSlot(Value * value) const;
	[[nodiscard]] const StackSlotInfo * slotOf(Value * value) const;
	[[nodiscard]] const StackSlotInfo * returnAddressSlot() const;
	[[nodiscard]] const StackSlotInfo * oldFramePointerSlot() const;

	void setFrameSize(int32_t size);
	void setOutgoingArgAreaSize(int32_t size);
	void addSlot(const StackSlotInfo & slot);

private:
	IRFunctionView func;
	int32_t totalFrameSize = 0;
	int32_t outgoingAreaSize = 0;
	std::vector<StackSlotInfo> slotInfos;
	std::unordered_map<Value *, std::size_t> slotIndex;
};

class FrameLayoutBuilder {

public:
	static FunctionFrameLayout build(IRFunctionView function);
};
