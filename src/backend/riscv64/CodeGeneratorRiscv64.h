#pragma once

#include "CodeGeneratorAsm.h"
#include "SimpleRegisterAllocator.h"

class CodeGeneratorRiscv64 : public CodeGeneratorAsm {

public:
	explicit CodeGeneratorRiscv64(Module * module);

	~CodeGeneratorRiscv64() override;

protected:
	void genHeader() override;

	void genDataSection() override;

	void genCodeSection(Function * func) override;

	void registerAllocation(Function * func) override;

	void stackAlloc(Function * func);

	void adjustFuncCallInsts(Function * func);

	void adjustFormalParamInsts(Function * func);

	void getIRValueStr(Value * val, std::string & str);

private:
	SimpleRegisterAllocator simpleRegisterAllocator;
};
