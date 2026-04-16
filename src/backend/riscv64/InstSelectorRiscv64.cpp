#include <string>

#include "InstSelectorRiscv64.h"
#include "ir/Instructions/FuncCallInstruction.h"
#include "ir/Instructions/GotoInstruction.h"
#include "ir/Instructions/LabelInstruction.h"

InstSelectorRiscv64::InstSelectorRiscv64(
	std::vector<Instruction *> & _irCode,
	ILocRiscv64 & _iloc,
	Function * _func,
	SimpleRegisterAllocator & allocator)
	: ir(_irCode), iloc(_iloc), func(_func), simpleRegisterAllocator(allocator)
{
	translator_handlers[IRInstOperator::IRINST_OP_ENTRY] = &InstSelectorRiscv64::translate_entry;
	translator_handlers[IRInstOperator::IRINST_OP_EXIT] = &InstSelectorRiscv64::translate_exit;
	translator_handlers[IRInstOperator::IRINST_OP_LABEL] = &InstSelectorRiscv64::translate_label;
	translator_handlers[IRInstOperator::IRINST_OP_GOTO] = &InstSelectorRiscv64::translate_goto;
	translator_handlers[IRInstOperator::IRINST_OP_ASSIGN] = &InstSelectorRiscv64::translate_assign;
	translator_handlers[IRInstOperator::IRINST_OP_ADD_I] = &InstSelectorRiscv64::translate_add_int32;
	translator_handlers[IRInstOperator::IRINST_OP_SUB_I] = &InstSelectorRiscv64::translate_sub_int32;
	translator_handlers[IRInstOperator::IRINST_OP_MUL_I] = &InstSelectorRiscv64::translate_mul_int32;
	translator_handlers[IRInstOperator::IRINST_OP_DIV_I] = &InstSelectorRiscv64::translate_div_int32;
	translator_handlers[IRInstOperator::IRINST_OP_MOD_I] = &InstSelectorRiscv64::translate_mod_int32;
	translator_handlers[IRInstOperator::IRINST_OP_FUNC_CALL] = &InstSelectorRiscv64::translate_call;
	translator_handlers[IRInstOperator::IRINST_OP_ARG] = &InstSelectorRiscv64::translate_arg;
}

InstSelectorRiscv64::~InstSelectorRiscv64()
{}

void InstSelectorRiscv64::run()
{
	for (auto * inst: ir) {
		if (!inst->isDead()) {
			translate(inst);
		}
	}
}

void InstSelectorRiscv64::translate(Instruction * inst)
{
	const IRInstOperator op = inst->getOp();
	auto pIter = translator_handlers.find(op);
	if (pIter == translator_handlers.end()) {
		return;
	}

	if (showLinearIR) {
		outputIRInstruction(inst);
	}

	(this->*(pIter->second))(inst);
}

void InstSelectorRiscv64::outputIRInstruction(Instruction * inst)
{
	std::string irStr;
	inst->toString(irStr);
	if (!irStr.empty()) {
		iloc.comment(irStr);
	}
}

void InstSelectorRiscv64::translate_nop(Instruction *)
{
	iloc.nop();
}

void InstSelectorRiscv64::translate_label(Instruction * inst)
{
	Instanceof(labelInst, LabelInstruction *, inst);
	iloc.label(labelInst->getName());
}

void InstSelectorRiscv64::translate_goto(Instruction * inst)
{
	Instanceof(gotoInst, GotoInstruction *, inst);
	iloc.jump(gotoInst->getTarget()->getName());
}

void InstSelectorRiscv64::translate_entry(Instruction *)
{
	iloc.allocStack(func, RISCV64_TMP_REG_NO);

	auto & params = func->getParams();
	for (int k = 0; k < static_cast<int>(params.size()) && k < PlatformRiscv64::argRegCount; ++k) {
		iloc.store_var(k, params[k], RISCV64_T3_REG_NO);
	}
}

void InstSelectorRiscv64::translate_exit(Instruction * inst)
{
	if (inst->getOperandsNum() > 0) {
		iloc.load_var(RISCV64_A0_REG_NO, inst->getOperand(0));
	}

	iloc.inst("ld", "ra", "-8(fp)");
	iloc.inst("ld", "t0", "-16(fp)");
	iloc.inst("mv", "sp", "fp");
	iloc.inst("mv", "fp", "t0");
	iloc.inst("ret");
}

void InstSelectorRiscv64::translate_assign(Instruction * inst)
{
	Value * result = inst->getOperand(0);
	Value * arg1 = inst->getOperand(1);

	iloc.load_var(RISCV64_T0_REG_NO, arg1);
	iloc.store_var(RISCV64_T0_REG_NO, result, RISCV64_T3_REG_NO);
}

void InstSelectorRiscv64::translate_two_operator(Instruction * inst, const std::string & operator_name)
{
	Value * result = inst;
	Value * arg1 = inst->getOperand(0);
	Value * arg2 = inst->getOperand(1);

	iloc.load_var(RISCV64_T0_REG_NO, arg1);
	iloc.load_var(RISCV64_T1_REG_NO, arg2);
	iloc.inst(
		operator_name,
		PlatformRiscv64::regName[RISCV64_TMP_REG_NO],
		PlatformRiscv64::regName[RISCV64_T0_REG_NO],
		PlatformRiscv64::regName[RISCV64_T1_REG_NO]);
	iloc.store_var(RISCV64_TMP_REG_NO, result, RISCV64_T3_REG_NO);
}

void InstSelectorRiscv64::translate_add_int32(Instruction * inst)
{
	translate_two_operator(inst, "addw");
}

void InstSelectorRiscv64::translate_sub_int32(Instruction * inst)
{
	translate_two_operator(inst, "subw");
}

void InstSelectorRiscv64::translate_mul_int32(Instruction * inst)
{
	translate_two_operator(inst, "mulw");
}

void InstSelectorRiscv64::translate_div_int32(Instruction * inst)
{
	translate_two_operator(inst, "divw");
}

void InstSelectorRiscv64::translate_mod_int32(Instruction * inst)
{
	translate_two_operator(inst, "remw");
}

void InstSelectorRiscv64::translate_call(Instruction * inst)
{
	auto * callInst = dynamic_cast<FuncCallInstruction *>(inst);
	const int32_t operandNum = callInst->getOperandsNum();

	for (int32_t k = 0; k < operandNum && k < PlatformRiscv64::argRegCount; ++k) {
		iloc.load_var(k, callInst->getOperand(k));
	}

	for (int32_t k = PlatformRiscv64::argRegCount; k < operandNum; ++k) {
		iloc.load_var(RISCV64_T0_REG_NO, callInst->getOperand(k));
		iloc.inst(
			"sw",
			PlatformRiscv64::regName[RISCV64_T0_REG_NO],
			std::to_string((k - PlatformRiscv64::argRegCount) * PlatformRiscv64::stackSlotSize) + "(sp)");
	}

	iloc.call_fun(callInst->getCalledName());

	if (callInst->hasResultValue()) {
		iloc.store_var(RISCV64_A0_REG_NO, callInst, RISCV64_T3_REG_NO);
	}
}

void InstSelectorRiscv64::translate_arg(Instruction *)
{}
