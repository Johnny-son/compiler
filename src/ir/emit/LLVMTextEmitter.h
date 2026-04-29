#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

class Function;
class GlobalVariable;
class Instruction;
class LabelInstruction;
class Module;
class Value;

class LLVMTextEmitter {
public:
	explicit LLVMTextEmitter(Module & module);

	bool emitToFile(const std::string & filePath);

private:
	struct FunctionEmitState {
		FILE * fp = nullptr;
		int32_t tempIndex = 0;
		std::unordered_map<const LabelInstruction *, std::string> labelNames;
	};

	Module & module;

	void emitModule(FILE * fp);
	void emitFunctionDeclaration(FILE * fp, Function * func);
	void emitFunctionDefinition(FILE * fp, Function * func);
	void emitGlobalVariable(FILE * fp, GlobalVariable * var);

	void collectLabelNames(Function * func, FunctionEmitState & state);
	void appendFunctionSignature(std::string & out, Function * func, bool withNames) const;
	std::string emitRValue(Value * value, FunctionEmitState & state);
	bool emitStore(Value * dst, Value * src, FunctionEmitState & state);

	std::string nextTemp(FunctionEmitState & state) const;
	std::string sanitizeLabelName(const std::string & rawName) const;
};
