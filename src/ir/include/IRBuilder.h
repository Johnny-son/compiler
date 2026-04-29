// LLVM风格IRBuilder

#pragma once

#include <string>
#include <vector>

#include "BasicBlock.h"
#include "Function.h"
#include "Module.h"
#include "ir/Instructions/AllocaInst.h"
#include "ir/Instructions/BinaryInst.h"
#include "ir/Instructions/BranchInst.h"
#include "ir/Instructions/CallInst.h"
#include "ir/Instructions/GetElementPtrInst.h"
#include "ir/Instructions/ICmpInst.h"
#include "ir/Instructions/LoadInst.h"
#include "ir/Instructions/PhiInst.h"
#include "ir/Instructions/ReturnInst.h"
#include "ir/Instructions/StoreInst.h"
#include "ir/Instructions/ZExtInst.h"

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

	ICmpInst * createICmpEQ(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpNE(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpSLT(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpSLE(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpSGT(Value * lhs, Value * rhs, const std::string & name = "");
	ICmpInst * createICmpSGE(Value * lhs, Value * rhs, const std::string & name = "");

	ZExtInst * createZExt(Value * value, Type * targetType, const std::string & name = "");
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
