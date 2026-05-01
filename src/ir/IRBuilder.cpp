// LLVM风格IRBuilder

#include "IRBuilder.h"

IRBuilder::IRBuilder(Module * module) : module(module)
{}

void IRBuilder::setInsertPoint(BasicBlock * block)
{
	currentBlock = block;
}

BasicBlock * IRBuilder::getInsertBlock() const
{
	return currentBlock;
}

Function * IRBuilder::currentFunction() const
{
	return currentBlock != nullptr ? currentBlock->getParent() : module->getCurrentFunction();
}

template<typename InstT>
InstT * IRBuilder::insert(InstT * inst, const std::string & name)
{
	if (inst == nullptr || currentBlock == nullptr || currentBlock->hasTerminator()) {
		delete inst;
		return nullptr;
	}

	if (inst->hasResultValue()) {
		Function * func = currentFunction();
		inst->setIRName(func != nullptr ? func->allocateLocalName(name) : name);
	}

	currentBlock->appendInst(inst);
	return inst;
}

AllocaInst * IRBuilder::createAlloca(Type * type, const std::string & name)
{
	std::string allocaName = name.empty() ? "" : name + ".addr";
	auto * inst = new AllocaInst(currentFunction(), type, "");
	if (!name.empty()) {
		inst->setName(name);
	}
	return insert(inst, allocaName);
}

LoadInst * IRBuilder::createLoad(Value * ptr, const std::string & name)
{
	return insert(new LoadInst(currentFunction(), ptr), name);
}

StoreInst * IRBuilder::createStore(Value * value, Value * ptr)
{
	return insert(new StoreInst(currentFunction(), value, ptr), "");
}

BinaryInst * IRBuilder::createAdd(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new BinaryInst(currentFunction(), BinaryInst::Op::Add, lhs, rhs), name);
}

BinaryInst * IRBuilder::createSub(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new BinaryInst(currentFunction(), BinaryInst::Op::Sub, lhs, rhs), name);
}

BinaryInst * IRBuilder::createMul(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new BinaryInst(currentFunction(), BinaryInst::Op::Mul, lhs, rhs), name);
}

BinaryInst * IRBuilder::createSDiv(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new BinaryInst(currentFunction(), BinaryInst::Op::SDiv, lhs, rhs), name);
}

BinaryInst * IRBuilder::createSRem(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new BinaryInst(currentFunction(), BinaryInst::Op::SRem, lhs, rhs), name);
}

BinaryInst * IRBuilder::createFAdd(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new BinaryInst(currentFunction(), BinaryInst::Op::FAdd, lhs, rhs), name);
}

BinaryInst * IRBuilder::createFSub(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new BinaryInst(currentFunction(), BinaryInst::Op::FSub, lhs, rhs), name);
}

BinaryInst * IRBuilder::createFMul(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new BinaryInst(currentFunction(), BinaryInst::Op::FMul, lhs, rhs), name);
}

BinaryInst * IRBuilder::createFDiv(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new BinaryInst(currentFunction(), BinaryInst::Op::FDiv, lhs, rhs), name);
}

ICmpInst * IRBuilder::createICmpEQ(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new ICmpInst(currentFunction(), ICmpInst::Predicate::EQ, lhs, rhs), name);
}

ICmpInst * IRBuilder::createICmpNE(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new ICmpInst(currentFunction(), ICmpInst::Predicate::NE, lhs, rhs), name);
}

ICmpInst * IRBuilder::createICmpSLT(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new ICmpInst(currentFunction(), ICmpInst::Predicate::SLT, lhs, rhs), name);
}

ICmpInst * IRBuilder::createICmpSLE(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new ICmpInst(currentFunction(), ICmpInst::Predicate::SLE, lhs, rhs), name);
}

ICmpInst * IRBuilder::createICmpSGT(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new ICmpInst(currentFunction(), ICmpInst::Predicate::SGT, lhs, rhs), name);
}

ICmpInst * IRBuilder::createICmpSGE(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new ICmpInst(currentFunction(), ICmpInst::Predicate::SGE, lhs, rhs), name);
}

FCmpInst * IRBuilder::createFCmpOEQ(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new FCmpInst(currentFunction(), FCmpInst::Predicate::OEQ, lhs, rhs), name);
}

FCmpInst * IRBuilder::createFCmpONE(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new FCmpInst(currentFunction(), FCmpInst::Predicate::ONE, lhs, rhs), name);
}

FCmpInst * IRBuilder::createFCmpOLT(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new FCmpInst(currentFunction(), FCmpInst::Predicate::OLT, lhs, rhs), name);
}

FCmpInst * IRBuilder::createFCmpOLE(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new FCmpInst(currentFunction(), FCmpInst::Predicate::OLE, lhs, rhs), name);
}

FCmpInst * IRBuilder::createFCmpOGT(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new FCmpInst(currentFunction(), FCmpInst::Predicate::OGT, lhs, rhs), name);
}

FCmpInst * IRBuilder::createFCmpOGE(Value * lhs, Value * rhs, const std::string & name)
{
	return insert(new FCmpInst(currentFunction(), FCmpInst::Predicate::OGE, lhs, rhs), name);
}

ZExtInst * IRBuilder::createZExt(Value * value, Type * targetType, const std::string & name)
{
	return insert(new ZExtInst(currentFunction(), value, targetType), name);
}

CastInst * IRBuilder::createSIToFP(Value * value, Type * targetType, const std::string & name)
{
	return insert(new CastInst(currentFunction(), CastInst::Op::SIToFP, value, targetType), name);
}

CastInst * IRBuilder::createFPToSI(Value * value, Type * targetType, const std::string & name)
{
	return insert(new CastInst(currentFunction(), CastInst::Op::FPToSI, value, targetType), name);
}

GetElementPtrInst * IRBuilder::createGEP(Value * basePtr, const std::vector<Value *> & indices, const std::string & name)
{
	return insert(new GetElementPtrInst(currentFunction(), basePtr, indices), name);
}

CallInst * IRBuilder::createCall(Function * callee, const std::vector<Value *> & args, const std::string & name)
{
	return insert(new CallInst(currentFunction(), callee, args, callee->getReturnType()), name);
}

PhiInst * IRBuilder::createPhi(Type * type, const std::string & name)
{
	return insert(new PhiInst(currentFunction(), type), name);
}

BranchInst * IRBuilder::createBr(BasicBlock * target)
{
	return insert(new BranchInst(currentFunction(), target), "");
}

BranchInst * IRBuilder::createCondBr(Value * cond, BasicBlock * trueBlock, BasicBlock * falseBlock)
{
	return insert(new BranchInst(currentFunction(), cond, trueBlock, falseBlock), "");
}

ReturnInst * IRBuilder::createRet(Value * value)
{
	return insert(new ReturnInst(currentFunction(), value), "");
}

ReturnInst * IRBuilder::createRetVoid()
{
	return insert(new ReturnInst(currentFunction()), "");
}
