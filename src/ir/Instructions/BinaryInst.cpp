// LLVM二元运算指令

#include "BinaryInst.h"

namespace {

const char * opName(BinaryInst::Op op)
{
	switch (op) {
		case BinaryInst::Op::Add:
			return "add";
		case BinaryInst::Op::Sub:
			return "sub";
		case BinaryInst::Op::Mul:
			return "mul";
		case BinaryInst::Op::SDiv:
			return "sdiv";
		case BinaryInst::Op::SRem:
			return "srem";
		case BinaryInst::Op::FAdd:
			return "fadd";
		case BinaryInst::Op::FSub:
			return "fsub";
		case BinaryInst::Op::FMul:
			return "fmul";
		case BinaryInst::Op::FDiv:
			return "fdiv";
	}
	return "add";
}

} // namespace

BinaryInst::BinaryInst(Function * func, Op op, Value * lhs, Value * rhs, const std::string & name)
	: Instruction(func, lhs->getType()), binaryOp(op)
{
	this->IRName = name;
	addOperand(lhs);
	addOperand(rhs);
}

BinaryInst::Op BinaryInst::getBinaryOp() const
{
	return binaryOp;
}

void BinaryInst::toString(std::string & str)
{
	Value * lhs = getOperand(0);
	Value * rhs = getOperand(1);
	str = getIRName() + " = " + opName(binaryOp) + " " + getType()->toString() + " " + lhs->getIRName() + ", " +
		  rhs->getIRName();
}
