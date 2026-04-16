#pragma once

#include <map>
#include <vector>

#include "Function.h"
#include "ILocRiscv64.h"
#include "Instruction.h"
#include "PlatformRiscv64.h"
#include "SimpleRegisterAllocator.h"

class InstSelectorRiscv64 {

	std::vector<Instruction *> & ir;

	ILocRiscv64 & iloc;

	Function * func;

protected:
	void translate(Instruction * inst);

	void translate_nop(Instruction * inst);

	void translate_entry(Instruction * inst);

	void translate_exit(Instruction * inst);

	void translate_assign(Instruction * inst);

	void translate_label(Instruction * inst);

	void translate_goto(Instruction * inst);

	void translate_add_int32(Instruction * inst);

	void translate_sub_int32(Instruction * inst);

	void translate_mul_int32(Instruction * inst);

	void translate_div_int32(Instruction * inst);

	void translate_mod_int32(Instruction * inst);

	void translate_two_operator(Instruction * inst, const std::string & operator_name);

	void translate_call(Instruction * inst);

	void translate_arg(Instruction * inst);

	void outputIRInstruction(Instruction * inst);

	using translate_handler = void (InstSelectorRiscv64::*)(Instruction *);

	std::map<IRInstOperator, translate_handler> translator_handlers;

	SimpleRegisterAllocator & simpleRegisterAllocator;

	bool showLinearIR = false;

public:
	InstSelectorRiscv64(
		std::vector<Instruction *> & _irCode,
		ILocRiscv64 & _iloc,
		Function * _func,
		SimpleRegisterAllocator & allocator);

	~InstSelectorRiscv64();

	void setShowLinearIR(bool show)
	{
		showLinearIR = show;
	}

	void run();
};
