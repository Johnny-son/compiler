// Conservative address-base analysis shared by memory optimization passes.

#include "MemoryAlias.h"

#include <cstddef>
#include <functional>

#include "AllocaInst.h"
#include "ArrayType.h"
#include "ConstInt.h"
#include "FormalParam.h"
#include "Function.h"
#include "GetElementPtrInst.h"
#include "GlobalVariable.h"
#include "PointerType.h"
#include "Type.h"
#include "Value.h"

namespace {

bool isPointerParam(Value * value)
{
	auto * param = dynamic_cast<FormalParam *>(value);
	return param != nullptr && param->getType() != nullptr && param->getType()->isPointerType();
}

bool isLocalAlloca(Value * value)
{
	return dynamic_cast<AllocaInst *>(value) != nullptr;
}

bool isGlobalObject(Value * value)
{
	return dynamic_cast<GlobalVariable *>(value) != nullptr;
}

Type * pointeeType(Value * ptr)
{
	if (auto * global = dynamic_cast<GlobalVariable *>(ptr); global != nullptr) {
		return global->getType();
	}
	if (auto * alloca = dynamic_cast<AllocaInst *>(ptr); alloca != nullptr) {
		return alloca->getAllocatedType();
	}
	auto * ptrType = dynamic_cast<PointerType *>(ptr != nullptr ? ptr->getType() : nullptr);
	if (ptrType == nullptr) {
		return ptr != nullptr ? ptr->getType() : nullptr;
	}
	return const_cast<Type *>(ptrType->getPointeeType());
}

int64_t gepScaleForIndex(Type *& currentType, std::size_t indexNo)
{
	Type * scaledType = currentType;
	if (indexNo > 0) {
		if (auto * arrayType = dynamic_cast<ArrayType *>(currentType); arrayType != nullptr) {
			scaledType = arrayType->getElementType();
			currentType = scaledType;
		}
	}
	if (scaledType == nullptr || scaledType->getSize() <= 0) {
		return -1;
	}
	return scaledType->getSize();
}

AliasLocation unknownLocation()
{
	return AliasLocation{};
}

AliasLocation exactLocation(Value * base, int64_t offset)
{
	AliasLocation location;
	location.known = true;
	location.base = base;
	location.offsetKnown = true;
	location.offset = offset;
	return location;
}

AliasLocation unknownOffsetLocation(Value * base)
{
	AliasLocation location;
	location.known = true;
	location.base = base;
	location.offsetKnown = false;
	location.offset = 0;
	return location;
}

} // namespace

std::size_t MemoryLocationKeyHash::operator()(const MemoryLocationKey & key) const
{
	auto baseHash = std::hash<Value *>{}(key.base);
	auto offsetHash = std::hash<int64_t>{}(key.offset);
	return baseHash ^ (offsetHash + 0x9e3779b97f4a7c15ULL + (baseHash << 6U) + (baseHash >> 2U));
}

MemoryAliasAnalysis::MemoryAliasAnalysis(Function * function) : function(function)
{}

AliasLocation MemoryAliasAnalysis::location(Value * value)
{
	if (value == nullptr) {
		return unknownLocation();
	}
	auto iter = cache.find(value);
	if (iter != cache.end()) {
		return iter->second;
	}
	if (visiting.find(value) != visiting.end()) {
		return unknownLocation();
	}

	visiting.insert(value);
	auto result = computeLocation(value);
	visiting.erase(value);
	cache[value] = result;
	return result;
}

bool MemoryAliasAnalysis::isExact(const AliasLocation & location) const
{
	return location.known && location.base != nullptr && location.offsetKnown;
}

MemoryLocationKey MemoryAliasAnalysis::keyFor(const AliasLocation & location) const
{
	return MemoryLocationKey{location.base, location.offset};
}

AliasLocation MemoryAliasAnalysis::locationFor(const MemoryLocationKey & key) const
{
	return exactLocation(key.base, key.offset);
}

bool MemoryAliasAnalysis::mustAlias(const AliasLocation & lhs, const AliasLocation & rhs) const
{
	if (!lhs.known || !rhs.known || lhs.base == nullptr || rhs.base == nullptr) {
		return false;
	}
	return lhs.base == rhs.base && lhs.offsetKnown && rhs.offsetKnown && lhs.offset == rhs.offset;
}

bool MemoryAliasAnalysis::neverAlias(const AliasLocation & lhs, const AliasLocation & rhs) const
{
	if (!lhs.known || !rhs.known || lhs.base == nullptr || rhs.base == nullptr) {
		return false;
	}
	return canProveDistinctBase(lhs.base, rhs.base);
}

bool MemoryAliasAnalysis::mayAlias(const AliasLocation & lhs, const AliasLocation & rhs) const
{
	return !neverAlias(lhs, rhs);
}

AliasLocation MemoryAliasAnalysis::computeLocation(Value * value)
{
	if (isLocalAlloca(value) || isGlobalObject(value) || isPointerParam(value)) {
		return exactLocation(value, 0);
	}

	auto * gep = dynamic_cast<GetElementPtrInst *>(value);
	if (gep == nullptr) {
		return unknownLocation();
	}

	auto baseLocation = location(gep->getBasePointer());
	if (!baseLocation.known || baseLocation.base == nullptr) {
		return unknownLocation();
	}

	bool offsetKnown = baseLocation.offsetKnown;
	int64_t offset = baseLocation.offset;
	Type * currentType = pointeeType(gep->getBasePointer());
	const auto indices = gep->getIndices();
	for (std::size_t indexNo = 0; indexNo < indices.size(); ++indexNo) {
		const int64_t scale = gepScaleForIndex(currentType, indexNo);
		auto * constIndex = dynamic_cast<ConstInt *>(indices[indexNo]);
		if (constIndex == nullptr || scale <= 0) {
			offsetKnown = false;
			continue;
		}
		if (offsetKnown) {
			offset += static_cast<int64_t>(constIndex->getVal()) * scale;
		}
	}

	return offsetKnown ? exactLocation(baseLocation.base, offset) : unknownOffsetLocation(baseLocation.base);
}

bool MemoryAliasAnalysis::canProveDistinctBase(Value * lhs, Value * rhs) const
{
	if (lhs == rhs || lhs == nullptr || rhs == nullptr) {
		return false;
	}

	const bool lhsLocal = isLocalAlloca(lhs);
	const bool rhsLocal = isLocalAlloca(rhs);
	const bool lhsGlobal = isGlobalObject(lhs);
	const bool rhsGlobal = isGlobalObject(rhs);

	if (lhsLocal && rhsLocal) {
		return true;
	}
	if ((lhsLocal && rhsGlobal) || (lhsGlobal && rhsLocal)) {
		return true;
	}
	if (lhsGlobal && rhsGlobal) {
		return true;
	}

	// Pointer parameters may alias each other or globals passed by the caller.
	(void) function;
	return false;
}
