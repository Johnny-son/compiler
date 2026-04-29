// LLVM风格基本块

#include "BasicBlock.h"

#include "Instruction.h"

BasicBlock::BasicBlock(Function * parent, std::string name) : Value(LabelType::getType()), parent(parent)
{
	this->name = std::move(name);
	this->IRName = this->name;
}

BasicBlock::~BasicBlock()
{
	for (auto * inst: instructions) {
		delete inst;
	}
	instructions.clear();
}

Function * BasicBlock::getParent() const
{
	return parent;
}

void BasicBlock::appendInst(Instruction * inst)
{
	if (inst == nullptr || hasTerminator()) {
		return;
	}
	instructions.push_back(inst);
}

void BasicBlock::addInst(Instruction * inst)
{
	appendInst(inst);
}

std::vector<Instruction *> & BasicBlock::getInstructions()
{
	return instructions;
}

const std::vector<Instruction *> & BasicBlock::getInstructions() const
{
	return instructions;
}

Instruction * BasicBlock::getTerminator() const
{
	if (!instructions.empty() && instructions.back()->isTerminator()) {
		return instructions.back();
	}
	return nullptr;
}

bool BasicBlock::hasTerminator() const
{
	return getTerminator() != nullptr;
}

void BasicBlock::toString(std::string & str) const
{
	str = name + ":\n";
	for (auto * inst: instructions) {
		std::string instStr;
		inst->toString(instStr);
		if (!instStr.empty()) {
			str += "  " + instStr + "\n";
		}
	}
}
