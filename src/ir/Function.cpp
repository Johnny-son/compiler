// 函数实现

#include <cctype>
#include <string>

#include "Function.h"
#include "BasicBlock.h"

Function::Function(std::string _name, FunctionType * _type, bool _builtin)
	: GlobalValue(_type, _name), returnType(_type->getReturnType()), builtIn(_builtin)
{
	setAlignment(1);
}

Function::~Function()
{
	Delete();
}

Type * Function::getReturnType()
{
	return returnType;
}

std::vector<FormalParam *> & Function::getParams()
{
	return params;
}

bool Function::isBuiltin()
{
	return builtIn;
}

void Function::toString(std::string & str)
{
	if (builtIn) {
		return;
	}

	str = "define " + getReturnType()->toString() + " " + getIRName() + "(";

	bool firstParam = true;
	for (auto * param: params) {
		if (!firstParam) {
			str += ", ";
		}
		firstParam = false;
		str += param->getType()->toString() + " " + param->getIRName();
	}

	str += ") {\n";
	for (auto * block: basicBlocks) {
		std::string blockStr;
		block->toString(blockStr);
		str += blockStr;
	}
	str += "}\n";
}

BasicBlock * Function::createBlock(const std::string & name)
{
	std::string baseName = name.empty() ? "bb" + std::to_string(basicBlocks.size()) : name;
	for (char & ch: baseName) {
		if (ch == '.') {
			continue;
		}
		if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
			ch = '_';
		}
	}

	std::string blockName = baseName;
	int32_t suffix = 0;
	while (usedBlockNames.find(blockName) != usedBlockNames.end()) {
		blockName = baseName + "." + std::to_string(++suffix);
	}
	usedBlockNames.insert(blockName);

	auto * block = new BasicBlock(this, blockName);
	basicBlocks.push_back(block);
	return block;
}

BasicBlock * Function::getEntryBlock() const
{
	return basicBlocks.empty() ? nullptr : basicBlocks.front();
}

const std::vector<BasicBlock *> & Function::getBasicBlocks() const
{
	return basicBlocks;
}

std::string Function::allocateLocalName(const std::string & hint)
{
	std::string candidate;
	if (hint.empty()) {
		do {
			candidate = "%" + std::to_string(nameCounter++);
		} while (usedLocalNames.find(candidate) != usedLocalNames.end());
	} else {
		candidate = hint[0] == '%' ? hint : "%" + hint;
		for (char & ch: candidate) {
			if (ch == '%' || ch == '.') {
				continue;
			}
			if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
				ch = '_';
			}
		}
		std::string base = candidate;
		int32_t suffix = 0;
		while (usedLocalNames.find(candidate) != usedLocalNames.end()) {
			candidate = base + "." + std::to_string(++suffix);
		}
	}

	usedLocalNames.insert(candidate);
	return candidate;
}

int Function::getMaxFuncCallArgCnt()
{
	return maxFuncCallArgCnt;
}

void Function::setMaxFuncCallArgCnt(int count)
{
	maxFuncCallArgCnt = count;
}

void Function::Delete()
{
	for (auto * block: basicBlocks) {
		delete block;
	}
	basicBlocks.clear();
}

void Function::renameIR()
{
	// 新 MiniLLVM 路径在构造指令时已经完成命名。
}
