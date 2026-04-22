#include "backend/include/IRAdapter.h"

#include <string>

#include "ir/include/Function.h"
#include "ir/include/Instruction.h"
#include "ir/include/Module.h"
#include "ir/include/Type.h"
#include "ir/include/Value.h"
#include "ir/Instructions/FuncCallInstruction.h"
#include "ir/Instructions/GotoInstruction.h"
#include "ir/Values/ConstInt.h"
#include "ir/Values/FormalParam.h"
#include "ir/Values/GlobalVariable.h"
#include "ir/Values/LocalVariable.h"
#include "ir/Values/MemVariable.h"

namespace {

IRValueKind classifyValue(Value * value)
{
	if (value == nullptr) {
		return IRValueKind::Invalid;
	}

	if (dynamic_cast<ConstInt *>(value) != nullptr) {
		return IRValueKind::ConstInt;
	}

	if (dynamic_cast<Function *>(value) != nullptr) {
		return IRValueKind::Function;
	}

	if (dynamic_cast<GlobalVariable *>(value) != nullptr) {
		return IRValueKind::GlobalVariable;
	}

	if (dynamic_cast<FormalParam *>(value) != nullptr) {
		return IRValueKind::FormalParam;
	}

	if (dynamic_cast<LocalVariable *>(value) != nullptr) {
		return IRValueKind::LocalVariable;
	}

	if (dynamic_cast<MemVariable *>(value) != nullptr) {
		return IRValueKind::MemoryVariable;
	}

	if (dynamic_cast<Instruction *>(value) != nullptr) {
		return IRValueKind::InstructionResult;
	}

	return IRValueKind::Unknown;
}

IRInstKind classifyInstruction(Instruction * inst)
{
	if (inst == nullptr) {
		return IRInstKind::Invalid;
	}

	switch (inst->getOp()) {
		case IRInstOperator::IRINST_OP_ENTRY:
			return IRInstKind::Entry;
		case IRInstOperator::IRINST_OP_EXIT:
			return IRInstKind::Exit;
		case IRInstOperator::IRINST_OP_LABEL:
			return IRInstKind::Label;
		case IRInstOperator::IRINST_OP_GOTO:
			return IRInstKind::Goto;
		case IRInstOperator::IRINST_OP_ADD_I:
			return IRInstKind::Add;
		case IRInstOperator::IRINST_OP_SUB_I:
			return IRInstKind::Sub;
		case IRInstOperator::IRINST_OP_MUL_I:
			return IRInstKind::Mul;
		case IRInstOperator::IRINST_OP_DIV_I:
			return IRInstKind::Div;
		case IRInstOperator::IRINST_OP_MOD_I:
			return IRInstKind::Mod;
		case IRInstOperator::IRINST_OP_ASSIGN:
			return IRInstKind::Assign;
		case IRInstOperator::IRINST_OP_FUNC_CALL:
			return IRInstKind::FuncCall;
		case IRInstOperator::IRINST_OP_ARG:
			return IRInstKind::Arg;
		default:
			return IRInstKind::Unknown;
	}
}

} // namespace

IRValueView::IRValueView(Value * value) : value(value)
{}

bool IRValueView::valid() const
{
	return value != nullptr;
}

Value * IRValueView::raw() const
{
	return value;
}

Type * IRValueView::type() const
{
	return value != nullptr ? value->getType() : nullptr;
}

IRValueKind IRValueView::kind() const
{
	return classifyValue(value);
}

bool IRValueView::isConstantInt() const
{
	return kind() == IRValueKind::ConstInt;
}

bool IRValueView::isGlobalVariable() const
{
	return kind() == IRValueKind::GlobalVariable;
}

bool IRValueView::isFunction() const
{
	return kind() == IRValueKind::Function;
}

bool IRValueView::isFormalParam() const
{
	return kind() == IRValueKind::FormalParam;
}

bool IRValueView::isLocalVariable() const
{
	return kind() == IRValueKind::LocalVariable;
}

bool IRValueView::isMemoryVariable() const
{
	return kind() == IRValueKind::MemoryVariable;
}

bool IRValueView::isInstructionResult() const
{
	return kind() == IRValueKind::InstructionResult;
}

std::string IRValueView::name() const
{
	return value != nullptr ? value->getName() : "";
}

std::string IRValueView::irName() const
{
	return value != nullptr ? value->getIRName() : "";
}

int32_t IRValueView::intValue(int32_t fallback) const
{
	auto * constInt = dynamic_cast<ConstInt *>(value);
	return constInt != nullptr ? constInt->getVal() : fallback;
}

IRInstView::IRInstView(Instruction * inst) : inst(inst)
{}

bool IRInstView::valid() const
{
	return inst != nullptr;
}

Instruction * IRInstView::raw() const
{
	return inst;
}

IRInstKind IRInstView::kind() const
{
	return classifyInstruction(inst);
}

std::string IRInstView::irName() const
{
	return inst != nullptr ? inst->getIRName() : "";
}

Type * IRInstView::type() const
{
	return inst != nullptr ? inst->getType() : nullptr;
}

bool IRInstView::hasResult() const
{
	return inst != nullptr && inst->hasResultValue();
}

IRValueView IRInstView::result() const
{
	return hasResult() ? IRValueView(inst) : IRValueView();
}

std::size_t IRInstView::operandCount() const
{
	return inst != nullptr ? static_cast<std::size_t>(inst->getOperandsNum()) : 0;
}

IRValueView IRInstView::operand(std::size_t index) const
{
	if (inst == nullptr || index >= operandCount()) {
		return IRValueView();
	}

	return IRValueView(inst->getOperand(static_cast<int32_t>(index)));
}

std::vector<IRValueView> IRInstView::operands() const
{
	std::vector<IRValueView> result;
	if (inst == nullptr) {
		return result;
	}

	result.reserve(operandCount());
	for (int32_t index = 0; index < inst->getOperandsNum(); ++index) {
		result.emplace_back(inst->getOperand(index));
	}

	return result;
}

Function * IRInstView::calledFunctionRaw() const
{
	auto * callInst = dynamic_cast<FuncCallInstruction *>(inst);
	return callInst != nullptr ? callInst->calledFunction : nullptr;
}

std::string IRInstView::calledFunctionName() const
{
	auto * callInst = dynamic_cast<FuncCallInstruction *>(inst);
	return callInst != nullptr ? callInst->getCalledName() : "";
}

LabelInstruction * IRInstView::targetLabelRaw() const
{
	auto * gotoInst = dynamic_cast<GotoInstruction *>(inst);
	return gotoInst != nullptr ? gotoInst->getTarget() : nullptr;
}

IRFunctionView::IRFunctionView(Function * func) : func(func)
{}

bool IRFunctionView::valid() const
{
	return func != nullptr;
}

Function * IRFunctionView::raw() const
{
	return func;
}

std::string IRFunctionView::name() const
{
	return func != nullptr ? func->getName() : "";
}

std::string IRFunctionView::irName() const
{
	return func != nullptr ? func->getIRName() : "";
}

Type * IRFunctionView::returnType() const
{
	return func != nullptr ? func->getReturnType() : nullptr;
}

bool IRFunctionView::isBuiltin() const
{
	return func != nullptr && func->isBuiltin();
}

std::vector<IRValueView> IRFunctionView::params() const
{
	std::vector<IRValueView> result;
	if (func == nullptr) {
		return result;
	}

	auto & params = func->getParams();
	result.reserve(params.size());
	for (auto * param: params) {
		result.emplace_back(param);
	}

	return result;
}

std::vector<IRValueView> IRFunctionView::locals() const
{
	std::vector<IRValueView> result;
	if (func == nullptr) {
		return result;
	}

	auto & locals = func->getVarValues();
	result.reserve(locals.size());
	for (auto * local: locals) {
		result.emplace_back(local);
	}

	return result;
}

std::vector<IRInstView> IRFunctionView::instructions() const
{
	std::vector<IRInstView> result;
	if (func == nullptr) {
		return result;
	}

	auto & insts = func->getInterCode().getInsts();
	result.reserve(insts.size());
	for (auto * inst: insts) {
		result.emplace_back(inst);
	}

	return result;
}

IRModuleView::IRModuleView(Module * module) : module(module)
{}

bool IRModuleView::valid() const
{
	return module != nullptr;
}

Module * IRModuleView::raw() const
{
	return module;
}

std::string IRModuleView::name() const
{
	return module != nullptr ? module->getName() : "";
}

std::vector<IRValueView> IRModuleView::globals() const
{
	std::vector<IRValueView> result;
	if (module == nullptr) {
		return result;
	}

	auto & globals = module->getGlobalVariables();
	result.reserve(globals.size());
	for (auto * global: globals) {
		result.emplace_back(global);
	}

	return result;
}

std::vector<IRFunctionView> IRModuleView::functions() const
{
	std::vector<IRFunctionView> result;
	if (module == nullptr) {
		return result;
	}

	auto & funcs = module->getFunctionList();
	result.reserve(funcs.size());
	for (auto * func: funcs) {
		result.emplace_back(func);
	}

	return result;
}

IRModuleView IRAdapter::adapt(Module * module)
{
	return IRModuleView(module);
}
