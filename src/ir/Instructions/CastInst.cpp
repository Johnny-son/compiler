// LLVM类型转换指令

#include "CastInst.h"

namespace {

const char * opName(CastInst::Op op)
{
	switch (op) {
		case CastInst::Op::SIToFP:
			return "sitofp";
		case CastInst::Op::FPToSI:
			return "fptosi";
	}
	return "sitofp";
}

} // namespace

CastInst::CastInst(Function * func, Op op, Value * value, Type * targetType, const std::string & name)
	: Instruction(func, targetType), castOp(op)
{
	this->IRName = name;
	addOperand(value);
}

CastInst::Op CastInst::getCastOp() const
{
	return castOp;
}

void CastInst::toString(std::string & str)
{
	Value * value = getOperand(0);
	str = getIRName() + " = " + opName(castOp) + " " + value->getType()->toString() + " " + value->getIRName() +
		  " to " + getType()->toString();
}
