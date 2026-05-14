// LLVM风格IRBuilder

#pragma once

#include <string>
#include <vector>

#include "BasicBlock.h"
#include "Function.h"
#include "Module.h"
#include "AllocaInst.h"
#include "BinaryInst.h"
#include "BranchInst.h"
#include "CastInst.h"
#include "CallInst.h"
#include "FCmpInst.h"
#include "GetElementPtrInst.h"
#include "ICmpInst.h"
#include "LoadInst.h"
#include "PhiInst.h"
#include "ReturnInst.h"
#include "StoreInst.h"
#include "ZExtInst.h"

class IRBuilder {
public:
	explicit IRBuilder(Module * module);

	void setInsertPoint(BasicBlock * block);
	BasicBlock * getInsertBlock() const;

	AllocaInst * createAlloca(Type * type, const std::string & name = "");
	LoadInst * createLoad(Value * ptr, const std::string & name = "");
	StoreInst * createStore(Value * value, Value * ptr);

	BinaryInst * createAdd(Value * lhs, Value * rhs, const std::string & name = "");
	BinaryInst * createSub(Value * lhs, Value * rhs, const std::string & name = "");
	BinaryInst * createMul(Value * lhs, Value * rhs, const std::string & name = "");
	BinaryInst * createSDiv(Value * lhs, Value * rhs, const std::string & name = "");
	BinaryInst * createSRem(Value * lhs, Value * rhs, const std::string & name = "");
	BinaryInst * createFAdd(Value * lhs, Value * rhs, const std::string & name = "");
	BinaryInst * createFSub(Value * lhs, Value * rhs, const std::string & name = "");
	BinaryInst * createFMul(Value * lhs, Value * rhs, const std::string & name = "");
	BinaryInst * createFDiv(Value * lhs, Value * rhs, const std::string & name = "");

	ICmpInst * createICmpEQ(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpNE(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpSLT(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpSLE(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpSGT(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpSGE(Value * lhs, Value * rhs, const std::string & name = "");
	FCmpInst * createFCmpOEQ(Value * lhs, Value * rhs, const std::string & name = "");
	FCmpInst * createFCmpONE(Value * lhs, Value * rhs, const std::string & name = "");
	FCmpInst * createFCmpOLT(Value * lhs, Value * rhs, const std::string & name = "");
	FCmpInst * createFCmpOLE(Value * lhs, Value * rhs, const std::string & name = "");
	FCmpInst * createFCmpOGT(Value * lhs, Value * rhs, const std::string & name = "");
	FCmpInst * createFCmpOGE(Value * lhs, Value * rhs, const std::string & name = "");

	ZExtInst * createZExt(Value * value, Type * targetType, const std::string & name = "");
	CastInst * createSIToFP(Value * value, Type * targetType, const std::string & name = "");
	CastInst * createFPToSI(Value * value, Type * targetType, const std::string & name = "");
	CastInst * createBitCast(Value * value, Type * targetType, const std::string & name = "");
	GetElementPtrInst * createGEP(Value * basePtr, const std::vector<Value *> & indices, const std::string & name = "");
	CallInst * createCall(Function * callee, const std::vector<Value *> & args, const std::string & name = "");
	PhiInst * createPhi(Type * type, const std::string & name = "");
	BranchInst * createBr(BasicBlock * target);
	BranchInst * createCondBr(Value * cond, BasicBlock * trueBlock, BasicBlock * falseBlock);
	ReturnInst * createRet(Value * value);
	ReturnInst * createRetVoid();

private:
	template<typename InstT>
	InstT * insert(InstT * inst, const std::string & name);

	Function * currentFunction() const;

	Module * module = nullptr;
	BasicBlock * currentBlock = nullptr;
};
