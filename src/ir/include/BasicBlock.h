// LLVM风格基本块

#pragma once

#include <string>
#include <vector>

#include "Value.h"
#include "ir/Types/LabelType.h"

class Function;
class Instruction;

class BasicBlock : public Value {
public:
	explicit BasicBlock(Function * parent, std::string name);
	~BasicBlock() override;

	Function * getParent() const;
	void appendInst(Instruction * inst);
	void addInst(Instruction * inst);
	std::vector<Instruction *> & getInstructions();
	const std::vector<Instruction *> & getInstructions() const;
	Instruction * getTerminator() const;
	bool hasTerminator() const;
	void toString(std::string & str) const;

	[[nodiscard]] std::string getIRName() const override
	{
		return name;
	}

private:
	Function * parent = nullptr;
	std::vector<Instruction *> instructions;
};
