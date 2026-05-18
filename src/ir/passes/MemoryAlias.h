// Conservative address-base analysis shared by memory optimization passes.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

class Function;
class Value;

struct AliasLocation {
	bool known = false;
	Value * base = nullptr;
	bool offsetKnown = false;
	int64_t offset = 0;
};

struct MemoryLocationKey {
	Value * base = nullptr;
	int64_t offset = 0;

	bool operator==(const MemoryLocationKey & other) const
	{
		return base == other.base && offset == other.offset;
	}
};

struct MemoryLocationKeyHash {
	std::size_t operator()(const MemoryLocationKey & key) const;
};

class MemoryAliasAnalysis {
public:
	explicit MemoryAliasAnalysis(Function * function);

	AliasLocation location(Value * value);
	bool isExact(const AliasLocation & location) const;
	MemoryLocationKey keyFor(const AliasLocation & location) const;
	AliasLocation locationFor(const MemoryLocationKey & key) const;

	bool mustAlias(const AliasLocation & lhs, const AliasLocation & rhs) const;
	bool neverAlias(const AliasLocation & lhs, const AliasLocation & rhs) const;
	bool mayAlias(const AliasLocation & lhs, const AliasLocation & rhs) const;

private:
	AliasLocation computeLocation(Value * value);
	bool canProveDistinctBase(Value * lhs, Value * rhs) const;

	Function * function = nullptr;
	std::unordered_map<Value *, AliasLocation> cache;
	std::unordered_set<Value *> visiting;
};
