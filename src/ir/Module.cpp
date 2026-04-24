// 符号表-模块类

#include "Module.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "symboltable/ScopeStack.h"
#include "ir/Instructions/ArgInstruction.h"
#include "ir/Instructions/BinaryInstruction.h"
#include "ir/Instructions/EntryInstruction.h"
#include "ir/Instructions/ExitInstruction.h"
#include "ir/Instructions/FuncCallInstruction.h"
#include "ir/Instructions/GotoInstruction.h"
#include "ir/Instructions/LabelInstruction.h"
#include "ir/Instructions/MoveInstruction.h"
#include "ir/Types/IntegerType.h"
#include "ir/Types/VoidType.h"
#include "ir/Values/ConstInt.h"
#include "ir/Values/FormalParam.h"
#include "ir/Values/LocalVariable.h"
#include "utils/Status.h"

namespace {

struct LLVMEmitState {
	FILE * fp = nullptr;
	int32_t tempIndex = 0;
	std::unordered_map<const LabelInstruction *, std::string> labelNames;
};

std::string nextLLVMTemp(LLVMEmitState & state)
{
	return "%llvm" + std::to_string(state.tempIndex++);
}

std::string sanitizeLabelName(const std::string & rawName)
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

void appendFunctionSignature(std::string & out, Function * func, bool withNames)
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

std::string emitLLVMRValue(Value * value, LLVMEmitState & state)
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
		std::string tempName = nextLLVMTemp(state);
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

bool emitLLVMStore(Value * dst, Value * src, LLVMEmitState & state)
{
	if ((dynamic_cast<LocalVariable *>(dst) == nullptr) && (dynamic_cast<GlobalVariable *>(dst) == nullptr)) {
		return false;
	}

	std::string typeName = dst->getType()->toString();
	std::string srcName = emitLLVMRValue(src, state);
	fprintf(
		state.fp,
		"  store %s %s, %s* %s\n",
		typeName.c_str(),
		srcName.c_str(),
		typeName.c_str(),
		dst->getIRName().c_str());
	return true;
}

void emitLLVMFunctionDeclaration(FILE * fp, Function * func)
{
	std::string decl = "declare ";
	appendFunctionSignature(decl, func, false);
	decl += "\n";
	fputs(decl.c_str(), fp);
}

void emitLLVMFunctionDefinition(FILE * fp, Function * func)
{
	LLVMEmitState state;
	state.fp = fp;

	for (auto * inst: func->getInterCode().getInsts()) {
		if (auto * labelInst = dynamic_cast<LabelInstruction *>(inst); labelInst != nullptr) {
			state.labelNames.insert({labelInst, sanitizeLabelName(labelInst->getIRName())});
		}
	}

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

			case IRInstOperator::IRINST_OP_ASSIGN: {
				Value * dst = inst->getOperand(0);
				Value * src = inst->getOperand(1);
				(void) emitLLVMStore(dst, src, state);
				break;
			}

			case IRInstOperator::IRINST_OP_ADD_I:
			case IRInstOperator::IRINST_OP_SUB_I:
			case IRInstOperator::IRINST_OP_MUL_I:
			case IRInstOperator::IRINST_OP_DIV_I:
			case IRInstOperator::IRINST_OP_MOD_I: {
				Value * src1 = inst->getOperand(0);
				Value * src2 = inst->getOperand(1);
				std::string lhs = emitLLVMRValue(src1, state);
				std::string rhs = emitLLVMRValue(src2, state);
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
					default:
						break;
				}

				fprintf(
					fp,
					"  %s = %s %s %s, %s\n",
					inst->getIRName().c_str(),
					opName.c_str(),
					inst->getType()->toString().c_str(),
					lhs.c_str(),
					rhs.c_str());
				break;
			}

			case IRInstOperator::IRINST_OP_FUNC_CALL: {
				auto * callInst = static_cast<FuncCallInstruction *>(inst);
				std::vector<std::string> argValues;
				argValues.reserve(inst->getOperandsNum());
				for (int32_t idx = 0; idx < inst->getOperandsNum(); ++idx) {
					argValues.push_back(emitLLVMRValue(inst->getOperand(idx), state));
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
					std::string retText = emitLLVMRValue(retValue, state);
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

} // namespace

Module::Module(std::string _name) : name(_name)
{
	// 创建作用域栈
	scopeStack = new ScopeStack();

	// 确保全局变量作用域入栈，这样全局变量才可以加入
	scopeStack->enterScope();

	// 加入内置函数putint
	(void) newFunction("putint", VoidType::getType(), {new FormalParam{IntegerType::getTypeInt(), ""}}, true);
	(void) newFunction("getint", IntegerType::getTypeInt(), {}, true);
}

/// @brief 进入作用域，如进入函数体块、语句块等
void Module::enterScope()
{
	scopeStack->enterScope();
}

/// @brief 退出作用域，如退出函数体块、语句块等
void Module::leaveScope()
{
	scopeStack->leaveScope();
}

///
/// @brief 在遍历抽象语法树的过程中，获取当前正在处理的函数。在函数外处理时返回空指针。
/// @return Function* 当前处理的函数对象
///
Function * Module::getCurrentFunction()
{
	return currentFunc;
}

///
/// @brief 设置当前正在处理的函数指针。函数外设置空指针
/// @param current 函数对象
///
void Module::setCurrentFunction(Function * current)
{
	currentFunc = current;
}

/// @brief 新建函数并放到函数列表中
/// @param name 函数名
/// @param returnType 返回值类型
/// @param params 形参列表
/// @param builtin 是否内置函数
/// @return 新建的函数对象实例
Function * Module::newFunction(std::string name, Type * returnType, std::vector<FormalParam *> params, bool builtin)
{
	// 先根据函数名查找函数，若找到则出错
	Function * tempFunc = findFunction(name);
	if (tempFunc) {
		// 函数已存在
		return nullptr;
	}

	// 根据形参创建形参类型清单
	std::vector<Type *> paramsType;
	paramsType.reserve(params.size());

	for (auto & param: params) {
		paramsType.push_back(param->getType());
	}

	/// 函数类型参数
	FunctionType * type = new FunctionType(returnType, paramsType);

	// 新建函数对象
	tempFunc = new Function(name, type, builtin);

	// 设置参数
	tempFunc->getParams().assign(params.begin(), params.end());

	insertFunctionDirectly(tempFunc);

	return tempFunc;
}

/// @brief 根据函数名查找函数信息
/// @param name 函数名
/// @return 函数信息
Function * Module::findFunction(std::string name)
{
	// 根据名字查找
	auto pIter = funcMap.find(name);
	if (pIter != funcMap.end()) {
		// 查找到
		return pIter->second;
	}

	return nullptr;
}

///
/// @brief 直接向函数的符号表中加入函数。需外部检查函数的存在性
/// @param func 要加入的函数
///
void Module::insertFunctionDirectly(Function * func)
{
	funcMap.insert({func->getName(), func});
	funcVector.emplace_back(func);
}

/// @brief Value直接插入到符号表中的全局变量中
/// @param name Value的名称
/// @param val Value信息
void Module::insertGlobalValueDirectly(GlobalVariable * val)
{
	globalVariableMap.emplace(val->getName(), val);
	globalVariableVector.push_back(val);
}

/// @brief Value直接插入到符号表中的全局变量中
/// @param name Value的名称
/// @param val Value信息
void Module::insertConstIntDirectly(ConstInt * val)
{
	constIntMap.emplace(val->getVal(), val);
}

/// @brief 新建一个整型数值的Value，并加入到符号表，用于后续释放空间
/// @param intVal 整数值
/// @return 常量Value
ConstInt * Module::newConstInt(int32_t intVal)
{
	// 查找整数字符串
	ConstInt * val = findConstInt(intVal);
	if (!val) {

		// 不存在，则创建整数常量Value
		val = new ConstInt(intVal);

		insertConstIntDirectly(val);
	}

	return val;
}

/// @brief 根据整数值获取当前符号
/// \param name 变量名
/// \return 变量对应的值
ConstInt * Module::findConstInt(int32_t val)
{
	ConstInt * temp = nullptr;

	auto pIter = constIntMap.find(val);
	if (pIter != constIntMap.end()) {
		// 查找到
		temp = pIter->second;
	}

	return temp;
}

/// @brief 在当前的作用域中查找，若没有查找到则创建局部变量或者全局变量。请注意不能创建临时变量
/// ! 该函数只有在AST遍历生成线性IR中使用，其它地方不能使用
/// @param type 变量类型
/// @param name 变量ID 局部变量时可以为空，目的为了SSA时创建临时的局部变量，
/// @return nullptr则说明变量已存在，否则为新建的变量
Value * Module::newVarValue(Type * type, std::string name)
{
	Value * retVal;
	std::string varName;

	// 若变量名有效，检查当前作用域中是否存在变量，如存在则语义错误
	// 反之，因无效需创建新的变量名，肯定不现在的不同，不需要查找
	if (!name.empty()) {
		Value * tempValue = scopeStack->findCurrentScope(name);
		if (tempValue) {
			// 变量存在，语义错误
			Status::Error("IR错误[E1400] 未知行 符号检查: 变量(%s)已经存在", name.c_str());
			return nullptr;
		}
	} else if (!currentFunc) {
		// 全局变量要求name不能为空串，必须有效
		Status::Error("IR错误[E1401] 未知行 符号检查: 全局变量名为空");
		return nullptr;
	}

	if (currentFunc) {

		// 获取变量作用域的层级
		int32_t scope_level;
		if (name.empty()) {
			scope_level = 1;
		} else {
			scope_level = scopeStack->getCurrentScopeLevel();
		}

		retVal = currentFunc->newLocalVarValue(type, name, scope_level);

	} else {
		retVal = newGlobalVariable(type, name);
	}

	// 增加做作用域中
	scopeStack->insertValue(retVal);

	return retVal;
}

/// @brief 查找变量，会根据作用域栈进行逐级查找。
/// ! 该函数只有在AST遍历生成线性IR中使用，其它地方不能使用
///
/// @param name 变量ID
/// @return 指针有效则找到，空指针未找到
Value * Module::findVarValue(std::string name)
{
	// 逐层级作用域查找
	Value * tempValue = scopeStack->findAllScope(name);

	return tempValue;
}

///
/// @brief 新建全局变量，要求name必须有效，并且加入到全局符号表中。不检查是否现有的符号表中是否存在。
/// @param type 类型
/// @param name 名字
/// @return Value* 全局变量
///
GlobalVariable * Module::newGlobalVariable(Type * type, std::string name)
{
	GlobalVariable * val = new GlobalVariable(type, name);

	insertGlobalValueDirectly(val);

	return val;
}

/// @brief 根据变量名获取当前符号(只管理全局变量和常量)
/// @param name 变量名或者常量名
/// @param create 变量查找不到时若为true则自动创建变量型Value，否则不创建
/// @return 变量对应的值
GlobalVariable * Module::findGlobalVariable(std::string name)
{
	GlobalVariable * temp = nullptr;

	auto pIter = globalVariableMap.find(name);
	if (pIter != globalVariableMap.end()) {
		// 查找到
		temp = pIter->second;
	}

	return temp;
}

/// @brief 清理注册的所有Value资源
void Module::Delete()
{
	// 清除所有的函数
	for (auto func: funcVector) {
		delete func;
	}

	// 清理全局变量
	for (auto var: globalVariableVector) {
		delete var;
	}

	// 相关列表清空
	globalVariableMap.clear();
	globalVariableVector.clear();

	funcMap.clear();
	funcVector.clear();
}

///
/// @brief 对IR指令中没有名字的全部命名
///
void Module::renameIR()
{
	// 全局变量目前都有名字，目前不存在没有名字的变量，因此
	// 对于全局变量的线性IR名称，只是在原来的名称前追加@即可

	// 遍历所有的函数，含局部变量名、形参、Label名、指令变量重命名
	for (auto func: funcVector) {
		func->renameIR();
	}
}

/// @brief 文本输出线性IR指令
/// @param filePath 输出文件路径
void Module::outputIR(const std::string & filePath)
{
	FILE * fp = fopen(filePath.c_str(), "w");
	if (nullptr == fp) {
		printf("fopen() failed\n");
		return;
	}

	// 先输出内置函数/外部函数声明
	for (auto func: funcVector) {
		if (func->isBuiltin()) {
			emitLLVMFunctionDeclaration(fp, func);
		}
	}

	if (!funcVector.empty()) {
		fputc('\n', fp);
	}

	// 输出全局变量定义。目前未初始化变量统一按0初始化处理。
	for (auto var: globalVariableVector) {
		int32_t initializer = var->hasInitializerValue() ? var->getInitializerInt() : 0;
		fprintf(
			fp,
			"%s = global %s %d, align %d\n",
			var->getIRName().c_str(),
			var->getType()->toString().c_str(),
			initializer,
			var->getAlignment());
	}

	if (!globalVariableVector.empty()) {
		fputc('\n', fp);
	}

	// 输出用户自定义函数定义
	for (auto func: funcVector) {
		if (!func->isBuiltin()) {
			emitLLVMFunctionDefinition(fp, func);
			fputc('\n', fp);
		}
	}

	fclose(fp);
}
