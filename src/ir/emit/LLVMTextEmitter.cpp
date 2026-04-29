#include "ir/emit/LLVMTextEmitter.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "ir/Instructions/CondBranchInstruction.h"
#include "ir/Instructions/FuncCallInstruction.h"
#include "ir/Instructions/GotoInstruction.h"
#include "ir/Instructions/LabelInstruction.h"
#include "ir/Types/IntegerType.h"
#include "ir/Values/ConstInt.h"
#include "ir/Values/FormalParam.h"
#include "ir/Values/GlobalVariable.h"
#include "ir/Values/LocalVariable.h"
#include "ir/include/Function.h"
#include "ir/include/Instruction.h"
#include "ir/include/Module.h"
#include "ir/include/Type.h"
#include "ir/include/Value.h"

LLVMTextEmitter::LLVMTextEmitter(Module & _module) : module(_module)
{}

bool LLVMTextEmitter::emitToFile(const std::string & filePath)
{
	FILE * fp = fopen(filePath.c_str(), "w");
	if (fp == nullptr) {
		return false;
	}

	emitModule(fp);
	fclose(fp);

	return true;
}

void LLVMTextEmitter::emitModule(FILE * fp)
{
	for (auto * func: module.getFunctionList()) {
		if (func->isBuiltin()) {
			emitFunctionDeclaration(fp, func);
		}
	}

	if (!module.getFunctionList().empty()) {
		fputc('\n', fp);
	}

	for (auto * var: module.getGlobalVariables()) {
		emitGlobalVariable(fp, var);
	}

	if (!module.getGlobalVariables().empty()) {
		fputc('\n', fp);
	}

	for (auto * func: module.getFunctionList()) {
		if (!func->isBuiltin()) {
			emitFunctionDefinition(fp, func);
			fputc('\n', fp);
		}
	}
}

void LLVMTextEmitter::emitGlobalVariable(FILE * fp, GlobalVariable * var)
{
	int32_t initializer = var->hasInitializerValue() ? var->getInitializerInt() : 0;
	fprintf(
		fp,
		"%s = global %s %d, align %d\n",
		var->getIRName().c_str(),
		var->getType()->toString().c_str(),
		initializer,
		var->getAlignment());
}

void LLVMTextEmitter::emitFunctionDeclaration(FILE * fp, Function * func)
{
	std::string decl = "declare ";
	appendFunctionSignature(decl, func, false);
	decl += "\n";
	fputs(decl.c_str(), fp);
}

void LLVMTextEmitter::emitFunctionDefinition(FILE * fp, Function * func)
{
	FunctionEmitState state;
	state.fp = fp;
	collectLabelNames(func, state);

	std::string def = "define ";
	appendFunctionSignature(def, func, true);
	def += " {\n";
	fputs(def.c_str(), fp);
	fputs("entry:\n", fp);

	for (auto * var: func->getVarValues()) {
		std::string typeName = var->getType()->toString();
		fprintf(fp, "  %s = alloca %s, align 4\n", var->getIRName().c_str(), typeName.c_str());
	}

	if (auto * retValue = func->getReturnValue(); retValue != nullptr) {
		std::string typeName = retValue->getType()->toString();
		fprintf(fp, "  store %s 0, %s* %s\n", typeName.c_str(), typeName.c_str(), retValue->getIRName().c_str());
	}

	bool blockTerminated = false;

	for (auto * inst: func->getInterCode().getInsts()) {
		switch (inst->getOp()) {
			case IRInstOperator::IRINST_OP_ENTRY:
				break;

			case IRInstOperator::IRINST_OP_LABEL: {
				auto * labelInst = static_cast<LabelInstruction *>(inst);
				const std::string & labelName = state.labelNames.at(labelInst);

				if (!blockTerminated) {
					fprintf(fp, "  br label %%%s\n", labelName.c_str());
				}

				fprintf(fp, "%s:\n", labelName.c_str());
				blockTerminated = false;
				break;
			}

			case IRInstOperator::IRINST_OP_GOTO: {
				auto * gotoInst = static_cast<GotoInstruction *>(inst);
				const std::string & targetName = state.labelNames.at(gotoInst->getTarget());
				fprintf(fp, "  br label %%%s\n", targetName.c_str());
				blockTerminated = true;
				break;
			}

			case IRInstOperator::IRINST_OP_COND_BR: {
				auto * brInst = static_cast<CondBranchInstruction *>(inst);
				std::string condValue = emitRValue(brInst->getCondition(), state);
				std::string condTemp = nextTemp(state);
				fprintf(fp, "  %s = icmp ne i32 %s, 0\n", condTemp.c_str(), condValue.c_str());
				fprintf(
					fp,
					"  br i1 %s, label %%%s, label %%%s\n",
					condTemp.c_str(),
					state.labelNames.at(brInst->getTrueTarget()).c_str(),
					state.labelNames.at(brInst->getFalseTarget()).c_str());
				blockTerminated = true;
				break;
			}

			case IRInstOperator::IRINST_OP_ASSIGN: {
				Value * dst = inst->getOperand(0);
				Value * src = inst->getOperand(1);
				(void) emitStore(dst, src, state);
				break;
			}

			case IRInstOperator::IRINST_OP_ADD_I:
			case IRInstOperator::IRINST_OP_SUB_I:
			case IRInstOperator::IRINST_OP_MUL_I:
			case IRInstOperator::IRINST_OP_DIV_I:
			case IRInstOperator::IRINST_OP_MOD_I:
			case IRInstOperator::IRINST_OP_CMP_EQ_I:
			case IRInstOperator::IRINST_OP_CMP_NE_I:
			case IRInstOperator::IRINST_OP_CMP_LT_I:
			case IRInstOperator::IRINST_OP_CMP_LE_I:
			case IRInstOperator::IRINST_OP_CMP_GT_I:
			case IRInstOperator::IRINST_OP_CMP_GE_I: {
				Value * src1 = inst->getOperand(0);
				Value * src2 = inst->getOperand(1);
				std::string lhs = emitRValue(src1, state);
				std::string rhs = emitRValue(src2, state);
				std::string opName;

				switch (inst->getOp()) {
					case IRInstOperator::IRINST_OP_ADD_I:
						opName = "add";
						break;
					case IRInstOperator::IRINST_OP_SUB_I:
						opName = "sub";
						break;
					case IRInstOperator::IRINST_OP_MUL_I:
						opName = "mul";
						break;
					case IRInstOperator::IRINST_OP_DIV_I:
						opName = "sdiv";
						break;
					case IRInstOperator::IRINST_OP_MOD_I:
						opName = "srem";
						break;
					case IRInstOperator::IRINST_OP_CMP_EQ_I:
						opName = "eq";
						break;
					case IRInstOperator::IRINST_OP_CMP_NE_I:
						opName = "ne";
						break;
					case IRInstOperator::IRINST_OP_CMP_LT_I:
						opName = "slt";
						break;
					case IRInstOperator::IRINST_OP_CMP_LE_I:
						opName = "sle";
						break;
					case IRInstOperator::IRINST_OP_CMP_GT_I:
						opName = "sgt";
						break;
					case IRInstOperator::IRINST_OP_CMP_GE_I:
						opName = "sge";
						break;
					default:
						break;
				}

				if (inst->getOp() == IRInstOperator::IRINST_OP_CMP_EQ_I || inst->getOp() == IRInstOperator::IRINST_OP_CMP_NE_I ||
					inst->getOp() == IRInstOperator::IRINST_OP_CMP_LT_I || inst->getOp() == IRInstOperator::IRINST_OP_CMP_LE_I ||
					inst->getOp() == IRInstOperator::IRINST_OP_CMP_GT_I || inst->getOp() == IRInstOperator::IRINST_OP_CMP_GE_I) {
					std::string cmpTemp = nextTemp(state);
					fprintf(
						fp,
						"  %s = icmp %s %s %s, %s\n",
						cmpTemp.c_str(),
						opName.c_str(),
						inst->getType()->toString().c_str(),
						lhs.c_str(),
						rhs.c_str());
					fprintf(
						fp,
						"  %s = zext i1 %s to %s\n",
						inst->getIRName().c_str(),
						cmpTemp.c_str(),
						inst->getType()->toString().c_str());
				} else {
					fprintf(
						fp,
						"  %s = %s %s %s, %s\n",
						inst->getIRName().c_str(),
						opName.c_str(),
						inst->getType()->toString().c_str(),
						lhs.c_str(),
						rhs.c_str());
				}
				break;
			}

			case IRInstOperator::IRINST_OP_FUNC_CALL: {
				auto * callInst = static_cast<FuncCallInstruction *>(inst);
				std::vector<std::string> argValues;
				argValues.reserve(inst->getOperandsNum());
				for (int32_t idx = 0; idx < inst->getOperandsNum(); ++idx) {
					argValues.push_back(emitRValue(inst->getOperand(idx), state));
				}

				if (inst->getType()->isVoidType()) {
					fprintf(
						fp,
						"  call %s %s(",
						callInst->calledFunction->getReturnType()->toString().c_str(),
						callInst->calledFunction->getIRName().c_str());
				} else {
					fprintf(
						fp,
						"  %s = call %s %s(",
						inst->getIRName().c_str(),
						callInst->calledFunction->getReturnType()->toString().c_str(),
						callInst->calledFunction->getIRName().c_str());
				}

				for (int32_t idx = 0; idx < inst->getOperandsNum(); ++idx) {
					if (idx != 0) {
						fputs(", ", fp);
					}

					Value * arg = inst->getOperand(idx);
					fprintf(fp, "%s %s", arg->getType()->toString().c_str(), argValues[idx].c_str());
				}

				fputs(")\n", fp);
				break;
			}

			case IRInstOperator::IRINST_OP_ARG:
				break;

			case IRInstOperator::IRINST_OP_EXIT: {
				if (inst->getOperandsNum() == 0) {
					fputs("  ret void\n", fp);
				} else {
					Value * retValue = inst->getOperand(0);
					std::string retText = emitRValue(retValue, state);
					fprintf(fp, "  ret %s %s\n", retValue->getType()->toString().c_str(), retText.c_str());
				}
				blockTerminated = true;
				break;
			}

			default:
				break;
		}
	}

	fputs("}\n", fp);
}

void LLVMTextEmitter::collectLabelNames(Function * func, FunctionEmitState & state)
{
	for (auto * inst: func->getInterCode().getInsts()) {
		if (auto * labelInst = dynamic_cast<LabelInstruction *>(inst); labelInst != nullptr) {
			state.labelNames.insert({labelInst, sanitizeLabelName(labelInst->getIRName())});
		}
	}
}

void LLVMTextEmitter::appendFunctionSignature(std::string & out, Function * func, bool withNames) const
{
	out += func->getReturnType()->toString();
	out += " ";
	out += func->getIRName();
	out += "(";

	bool first = true;
	for (auto * param: func->getParams()) {
		if (!first) {
			out += ", ";
		}
		first = false;

		out += param->getType()->toString();
		if (withNames) {
			out += " ";
			out += param->getIRName();
		}
	}

	out += ")";
}

std::string LLVMTextEmitter::emitRValue(Value * value, FunctionEmitState & state)
{
	if (dynamic_cast<ConstInt *>(value) != nullptr) {
		return value->getIRName();
	}

	if (dynamic_cast<FormalParam *>(value) != nullptr) {
		return value->getIRName();
	}

	if (auto * inst = dynamic_cast<Instruction *>(value); (inst != nullptr) && inst->hasResultValue()) {
		return inst->getIRName();
	}

	if ((dynamic_cast<LocalVariable *>(value) != nullptr) || (dynamic_cast<GlobalVariable *>(value) != nullptr)) {
		std::string tempName = nextTemp(state);
		std::string typeName = value->getType()->toString();
		fprintf(
			state.fp,
			"  %s = load %s, %s* %s\n",
			tempName.c_str(),
			typeName.c_str(),
			typeName.c_str(),
			value->getIRName().c_str());
		return tempName;
	}

	return value->getIRName();
}

bool LLVMTextEmitter::emitStore(Value * dst, Value * src, FunctionEmitState & state)
{
	if ((dynamic_cast<LocalVariable *>(dst) == nullptr) && (dynamic_cast<GlobalVariable *>(dst) == nullptr)) {
		return false;
	}

	std::string typeName = dst->getType()->toString();
	std::string srcName = emitRValue(src, state);
	fprintf(
		state.fp,
		"  store %s %s, %s* %s\n",
		typeName.c_str(),
		srcName.c_str(),
		typeName.c_str(),
		dst->getIRName().c_str());
	return true;
}

std::string LLVMTextEmitter::nextTemp(FunctionEmitState & state) const
{
	return "%llvm" + std::to_string(state.tempIndex++);
}

std::string LLVMTextEmitter::sanitizeLabelName(const std::string & rawName) const
{
	std::string name = rawName;
	if (!name.empty() && ((name[0] == '%') || (name[0] == '@'))) {
		name.erase(name.begin());
	}

	std::string result;
	result.reserve(name.size());

	for (unsigned char ch: name) {
		if (std::isalnum(ch) || (ch == '_')) {
			result.push_back(static_cast<char>(ch));
		} else {
			result.push_back('_');
		}
	}

	if (result.empty()) {
		result = "bb";
	}

	if (std::isdigit(static_cast<unsigned char>(result[0]))) {
		result = "bb_" + result;
	}

	return result;
}
