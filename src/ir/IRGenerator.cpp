#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "IRGenerator.h"
#include "ir/include/Function.h"
#include "ir/include/Type.h"
#include "ir/include/Value.h"
#include "ir/Types/ArrayType.h"
#include "ir/Types/FloatType.h"
#include "ir/Types/IntegerType.h"
#include "ir/Types/PointerType.h"
#include "ir/Values/ConstFloat.h"
#include "ir/Values/ConstInt.h"
#include "ir/Values/FormalParam.h"
#include "ir/Values/GlobalVariable.h"
#include "utils/Status.h"

namespace {

// 统一格式化IR错误详情，供可变参数错误上报复用
std::string ir_format_detail(const char * format, va_list args)
{
	if (format == nullptr) {
		return "";
	}

	va_list args_copy;
	va_copy(args_copy, args);
	const int message_size = std::vsnprintf(nullptr, 0, format, args_copy);
	va_end(args_copy);

	if (message_size < 0) {
		return format;
	}

	std::vector<char> buffer(static_cast<std::size_t>(message_size) + 1);
	std::vsnprintf(buffer.data(), buffer.size(), format, args);
	return std::string(buffer.data(), static_cast<std::size_t>(message_size));
}

// IR阶段统一错误输出，保持错误码、行号和类别格式一致
void report_ir_error(const char * error_code, int64_t lineno, const char * category, const char * detail_format, ...)
{
	va_list args;
	va_start(args, detail_format);
	std::string detail = ir_format_detail(detail_format, args);
	va_end(args);

	if (lineno > 0) {
		Status::Error(
			"IR错误[%s] 第%lld行 %s: %s",
			error_code,
			(long long) lineno,
			category != nullptr ? category : "未知类别",
			detail.c_str());
	} else {
		Status::Error(
			"IR错误[%s] 未知行 %s: %s",
			error_code,
			category != nullptr ? category : "未知类别",
			detail.c_str());
	}
}

} // namespace

// 构造函数
IRGenerator::IRGenerator(ast_node * _root, Module * _module) : root(_root), module(_module), builder(_module)
{
	/* 叶子节点 */
	ast2ir_handlers[ast_operator_type::AST_OP_LEAF_LITERAL_UINT] = &IRGenerator::ir_leaf_node_uint;
	ast2ir_handlers[ast_operator_type::AST_OP_LEAF_LITERAL_FLOAT] = &IRGenerator::ir_leaf_node_float;
	ast2ir_handlers[ast_operator_type::AST_OP_LEAF_VAR_ID] = &IRGenerator::ir_leaf_node_var_id;
	ast2ir_handlers[ast_operator_type::AST_OP_LEAF_TYPE] = &IRGenerator::ir_leaf_node_type;
	ast2ir_handlers[ast_operator_type::AST_OP_ARRAY_ACCESS] = &IRGenerator::ir_array_access;

	/* 表达式运算， 加减 */
	ast2ir_handlers[ast_operator_type::AST_OP_SUB] = &IRGenerator::ir_sub;
	ast2ir_handlers[ast_operator_type::AST_OP_ADD] = &IRGenerator::ir_add;
	ast2ir_handlers[ast_operator_type::AST_OP_MUL] = &IRGenerator::ir_mul;
	ast2ir_handlers[ast_operator_type::AST_OP_DIV] = &IRGenerator::ir_div;
	ast2ir_handlers[ast_operator_type::AST_OP_MOD] = &IRGenerator::ir_mod;
	ast2ir_handlers[ast_operator_type::AST_OP_EQ] = &IRGenerator::ir_eq;
	ast2ir_handlers[ast_operator_type::AST_OP_NE] = &IRGenerator::ir_ne;
	ast2ir_handlers[ast_operator_type::AST_OP_LT] = &IRGenerator::ir_lt;
	ast2ir_handlers[ast_operator_type::AST_OP_LE] = &IRGenerator::ir_le;
	ast2ir_handlers[ast_operator_type::AST_OP_GT] = &IRGenerator::ir_gt;
	ast2ir_handlers[ast_operator_type::AST_OP_GE] = &IRGenerator::ir_ge;
	ast2ir_handlers[ast_operator_type::AST_OP_LAND] = &IRGenerator::ir_logical_and;
	ast2ir_handlers[ast_operator_type::AST_OP_LOR] = &IRGenerator::ir_logical_or;
	ast2ir_handlers[ast_operator_type::AST_OP_NOT] = &IRGenerator::ir_logical_not;

	/* 语句 */
	ast2ir_handlers[ast_operator_type::AST_OP_ASSIGN] = &IRGenerator::ir_assign;
	ast2ir_handlers[ast_operator_type::AST_OP_RETURN] = &IRGenerator::ir_return;
	ast2ir_handlers[ast_operator_type::AST_OP_IF] = &IRGenerator::ir_if;
	ast2ir_handlers[ast_operator_type::AST_OP_WHILE] = &IRGenerator::ir_while;
	ast2ir_handlers[ast_operator_type::AST_OP_FOR] = &IRGenerator::ir_for;
	ast2ir_handlers[ast_operator_type::AST_OP_BREAK] = &IRGenerator::ir_break;
	ast2ir_handlers[ast_operator_type::AST_OP_CONTINUE] = &IRGenerator::ir_continue;

	/* 函数调用 */
	ast2ir_handlers[ast_operator_type::AST_OP_FUNC_CALL] = &IRGenerator::ir_function_call;

	/* 函数定义 */
	ast2ir_handlers[ast_operator_type::AST_OP_FUNC_DEF] = &IRGenerator::ir_function_define;
	ast2ir_handlers[ast_operator_type::AST_OP_FUNC_FORMAL_PARAMS] = &IRGenerator::ir_function_formal_params;

	/* 变量定义语句 */
	ast2ir_handlers[ast_operator_type::AST_OP_DECL_STMT] = &IRGenerator::ir_declare_statment;
	ast2ir_handlers[ast_operator_type::AST_OP_VAR_DECL] = &IRGenerator::ir_variable_declare;
	ast2ir_handlers[ast_operator_type::AST_OP_CONST_DECL] = &IRGenerator::ir_const_declaration;

	/* 语句块 */
	ast2ir_handlers[ast_operator_type::AST_OP_BLOCK] = &IRGenerator::ir_block;

	/* 编译单元 */
	ast2ir_handlers[ast_operator_type::AST_OP_COMPILE_UNIT] = &IRGenerator::ir_compile_unit;
}

// 遍历抽象语法树产生MiniLLVM IR，保存到Module中
bool IRGenerator::run()
{
	ast_node * node;

	// 从根节点进行遍历
	node = ir_visit_ast_node(root);

	return node != nullptr;
}

// 根据AST的节点运算符查找对应的翻译函数并执行翻译动作
ast_node * IRGenerator::ir_visit_ast_node(ast_node * node)
{
	// 空节点
	if (nullptr == node) {
		return nullptr;
	}

	bool result;

	std::unordered_map<ast_operator_type, ast2ir_handler_t>::const_iterator pIter;
	pIter = ast2ir_handlers.find(node->node_type);
	if (pIter == ast2ir_handlers.end()) {
		// 没有找到，则说明当前不支持
		result = (this->ir_default)(node);
	} else {
		result = (this->*(pIter->second))(node);
	}

	if (!result) {
		// 语义解析错误，则出错返回
		node = nullptr;
	}

	return node;
}

// 未知节点类型的节点处理
bool IRGenerator::ir_default(ast_node * node)
{
	// 未知节点视为IR阶段错误，避免静默吞掉不支持语法
	report_ir_error("E1000", node != nullptr ? node->line_no : -1, "节点分发", "不支持的AST节点类型(%d)", (int) node->node_type);
	return false;
}

// 编译单元AST节点翻译成线性中间IR
bool IRGenerator::ir_compile_unit(ast_node * node)
{
	module->setCurrentFunction(nullptr);

	// 第一阶段：翻译全局变量和常量声明，初始化仍处于全局上下文。
	// 函数形参数组维度可能引用前面的全局const，因此要先让这些const进入符号表。
	for (auto son: node->sons) {
		if (son != nullptr && son->node_type == ast_operator_type::AST_OP_DECL_STMT) {
			ast_node * son_node = ir_visit_ast_node(son);
			if (!son_node) {
				return false;
			}
		}
	}

	// 第二阶段：把所有函数签名注册进模块，支持先调用后定义和递归
	for (auto son: node->sons) {
		if (son != nullptr && son->node_type == ast_operator_type::AST_OP_FUNC_DEF) {
			if (!predeclare_function(son)) {
				return false;
			}
		}
	}

	// 第三阶段：真正翻译函数体
	for (auto son: node->sons) {
		if (son != nullptr && son->node_type == ast_operator_type::AST_OP_FUNC_DEF) {
			ast_node * son_node = ir_visit_ast_node(son);
			if (!son_node) {
				return false;
			}
		}
	}

	return true;
}

// 函数定义AST节点翻译成线性中间IR
bool IRGenerator::ir_function_define(ast_node * node)
{
	if (module->getCurrentFunction()) {
		report_ir_error("E1100", node != nullptr ? node->line_no : -1, "语义检查", "函数定义不允许嵌套");
		return false;
	}

	if (node == nullptr || node->sons.size() < 4) {
		report_ir_error("E1103", node != nullptr ? node->line_no : -1, "语义检查", "函数定义节点非法");
		return false;
	}

	// 正常路径下函数已经在编译单元阶段预声明；这里保留兜底逻辑避免直接崩掉
	if (module->findFunction(node->sons[1]->name) == nullptr) {
		if (!predeclare_function(node)) {
			return false;
		}
	}

	return ir_function_body(node);
}

// 形式参数AST节点翻译成线性中间IR
bool IRGenerator::ir_function_formal_params(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	if (currentFunc == nullptr) {
		report_ir_error("E1110", node != nullptr ? node->line_no : -1, "参数检查", "当前函数上下文为空，无法翻译形参");
		return false;
	}

	if (node->sons.size() != currentFunc->getParams().size()) {
		report_ir_error(
			"E1111",
			node != nullptr ? node->line_no : -1,
			"参数检查",
			"函数(%s)形参数量不一致，期望%zu个，实际%zu个",
			currentFunc->getName().c_str(),
			currentFunc->getParams().size(),
			node->sons.size());
		return false;
	}

	for (size_t i = 0; i < currentFunc->getParams().size(); ++i) {
		auto * param = currentFunc->getParams()[i];
		// AST形参节点与Module中的FormalParam一一对应
		auto * paramNode = node->sons[i];
		AllocaInst * localParam = createEntryAlloca(currentFunc, param->getType(), param->getName());
		if (localParam == nullptr || !module->bindValue(param->getName(), localParam, paramNode->line_no)) {
			report_ir_error(
				"E1112",
				paramNode != nullptr ? paramNode->line_no : (node != nullptr ? node->line_no : -1),
				"参数检查",
				"函数(%s)形参(%s)映射到局部变量失败",
				currentFunc->getName().c_str(),
				param->getName().c_str());
			return false;
		}

		// 形参进入函数体后统一按局部变量使用，这里先把FormalParam拷到局部槽位
		builder.createStore(param, localParam);
	}

	return true;
}

// 预声明函数签名，不翻译函数体，用于支持前向调用和递归
bool IRGenerator::predeclare_function(ast_node * node)
{
	if (node == nullptr || node->sons.size() < 4) {
		report_ir_error("E1103", node != nullptr ? node->line_no : -1, "语义检查", "函数定义节点非法");
		return false;
	}

	ast_node * type_node = node->sons[0];
	ast_node * name_node = node->sons[1];
	ast_node * param_node = node->sons[2];

	if (module->findFunction(name_node->name) != nullptr) {
		report_ir_error(
			"E1102",
			name_node != nullptr ? name_node->line_no : -1,
			"符号检查",
			"函数(%s)重复定义或符号冲突",
			name_node->name.c_str());
		return false;
	}

	std::vector<FormalParam *> formalParams;
	formalParams.reserve(param_node->sons.size());
	for (auto * param: param_node->sons) {
		if (param == nullptr || param->node_type != ast_operator_type::AST_OP_FUNC_FORMAL_PARAM) {
			report_ir_error(
				"E1101",
				name_node != nullptr ? name_node->line_no : -1,
				"参数检查",
				"函数(%s)形参节点非法",
				name_node->name.c_str());
			return false;
		}

		Type * paramType = buildFormalParamType(param);
		if (paramType == nullptr) {
			report_ir_error(
				"E1113",
				param != nullptr ? param->line_no : (name_node != nullptr ? name_node->line_no : -1),
				"参数检查",
				"函数(%s)形参(%s)数组维度非法",
				name_node->name.c_str(),
				param != nullptr ? param->name.c_str() : "<null>");
			return false;
		}

		param->type = paramType;
		formalParams.push_back(new FormalParam(paramType, param->name));
	}

	if (module->newFunction(name_node->name, type_node->type, formalParams) == nullptr) {
		report_ir_error(
			"E1102",
			name_node != nullptr ? name_node->line_no : -1,
			"符号检查",
			"函数(%s)重复定义或符号冲突",
			name_node->name.c_str());
		return false;
	}

	return true;
}

// 函数体AST节点翻译成线性中间IR
bool IRGenerator::ir_function_body(ast_node * node)
{
	ast_node * type_node = node->sons[0];
	ast_node * name_node = node->sons[1];
	ast_node * param_node = node->sons[2];
	ast_node * block_node = node->sons[3];

	Function * currentFunc = module->findFunction(name_node->name);
	if (currentFunc == nullptr) {
		report_ir_error(
			"E1104",
			name_node != nullptr ? name_node->line_no : -1,
			"符号检查",
			"函数(%s)未完成预声明",
			name_node->name.c_str());
		return false;
	}

	module->setCurrentFunction(currentFunc);
	module->enterScope();

	bool ok = false;
	do {
		BasicBlock * entryBlock = currentFunc->createBlock("entry");
		builder.setInsertPoint(entryBlock);

		if (!ir_function_formal_params(param_node)) {
			break;
		}

		// 函数体作用域已在进入函数时建立，因此函数体block本身不再重复enterScope
		block_node->needScope = false;
		if (!ir_block(block_node)) {
			break;
		}

		if (builder.getInsertBlock() != nullptr && !builder.getInsertBlock()->hasTerminator()) {
			if (type_node->type->isVoidType()) {
				builder.createRetVoid();
			} else {
				builder.createRet(zeroValueForType(type_node->type));
			}
		}
		ok = true;
	} while (false);

	builder.setInsertPoint(nullptr);
	module->setCurrentFunction(nullptr);
	module->leaveScope();

	return ok;
}

// 语句块（含函数体）AST节点翻译成线性中间IR
bool IRGenerator::ir_block(ast_node * node)
{
	// 进入作用域
	if (node->needScope) {
		module->enterScope();
	}

	std::vector<ast_node *>::iterator pIter;
	for (pIter = node->sons.begin(); pIter != node->sons.end(); ++pIter) {
		if (builder.getInsertBlock() != nullptr && builder.getInsertBlock()->hasTerminator()) {
			break;
		}

		// 遍历Block的每个语句，进行显示或者运算
		ast_node * temp = ir_visit_ast_node(*pIter);
		if (!temp) {
			return false;
		}
	}

	// 离开作用域
	if (node->needScope) {
		module->leaveScope();
	}

	return true;
}

// return节点翻译成线性中间IR
bool IRGenerator::ir_return(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	if (currentFunc == nullptr) {
		report_ir_error("E1122", node != nullptr ? node->line_no : -1, "返回语句", "return语句不在函数内");
		return false;
	}

	ast_node * right = nullptr;

	// return语句可能没有没有表达式，也可能有，因此这里必须进行区分判断
	if (!node->sons.empty()) {

		ast_node * son_node = node->sons[0];

		// 返回的表达式的指令保存在right节点中
		right = ir_visit_ast_node(son_node);
		if (!right) {
			return false;
		}
	}

	if (right != nullptr && currentFunc->getReturnType()->isVoidType()) {
		report_ir_error("E1120", node != nullptr ? node->line_no : -1, "返回语句", "void函数不能返回表达式");
		return false;
	}

	if (right == nullptr && !currentFunc->getReturnType()->isVoidType()) {
		report_ir_error("E1121", node != nullptr ? node->line_no : -1, "返回语句", "非void函数必须返回表达式");
		return false;
	}

	if (right) {
		node->val = convertValueToType(emitRValue(right->val, "retval"), currentFunc->getReturnType(), "retcast");
		if (node->val == nullptr) {
			report_ir_error("E1123", node != nullptr ? node->line_no : -1, "返回语句", "返回值类型转换失败");
			return false;
		}
		builder.createRet(node->val);
	} else {
		node->val = nullptr;
		builder.createRetVoid();
	}

	return true;
}

// 类型叶子节点翻译成线性中间IR
bool IRGenerator::ir_leaf_node_type(ast_node * node)
{
	// 不需要做什么，直接从节点中获取即可。

	return true;
}

// 无符号整数字面量叶子节点翻译成线性中间IR
bool IRGenerator::ir_leaf_node_uint(ast_node * node)
{
	ConstInt * val;

	// 新建一个整数常量Value
	val = module->newConstInt((int32_t) node->integer_val);

	node->val = val;

	return true;
}

// float字面量叶子节点翻译成MiniLLVM IR
bool IRGenerator::ir_leaf_node_float(ast_node * node)
{
	node->val = module->newConstFloat(node->float_val);
	return true;
}

// 标识符叶子节点翻译成线性中间IR，变量声明的不走这个语句
bool IRGenerator::ir_leaf_node_var_id(ast_node * node)
{
	Value * val = module->lookupValue(node->name);
	if (val == nullptr) {
		report_ir_error(
			"E1010",
			node != nullptr ? node->line_no : -1,
			"符号检查",
			"标识符(%s)未定义",
			node != nullptr ? node->name.c_str() : "<null>");
		return false;
	}

	node->val = val;

	return true;
}

// 数组访问节点翻译成MiniLLVM IR，结果保持为左值地址
bool IRGenerator::ir_array_access(ast_node * node)
{
	Value * base = module->lookupValue(node->name);
	if (base == nullptr) {
		report_ir_error(
			"E1010",
			node != nullptr ? node->line_no : -1,
			"符号检查",
			"标识符(%s)未定义",
			node != nullptr ? node->name.c_str() : "<null>");
		return false;
	}

	Value * gepBase = base;
	bool needLeadingZero = false;
	if (auto * alloca = dynamic_cast<AllocaInst *>(base); alloca != nullptr) {
		if (alloca->getAllocatedType()->isArrayType()) {
			needLeadingZero = true;
		} else if (alloca->getAllocatedType()->isPointerType()) {
			gepBase = builder.createLoad(base, node->name + ".base");
		}
	} else if (auto * global = dynamic_cast<GlobalVariable *>(base); global != nullptr) {
		needLeadingZero = global->getType()->isArrayType();
	}

	std::vector<Value *> indices;
	if (needLeadingZero) {
		indices.push_back(module->newConstInt(0));
	}

	for (auto * indexExpr: node->sons) {
		ast_node * indexNode = ir_visit_ast_node(indexExpr);
		if (indexNode == nullptr) {
			return false;
		}
		indices.push_back(emitRValue(indexNode->val, "idx"));
	}

	node->val = builder.createGEP(gepBase, indices, node->name + ".idx");
	if (node->val == nullptr) {
		report_ir_error("E1307", node != nullptr ? node->line_no : -1, "数组访问", "数组(%s)寻址失败", node->name.c_str());
		return false;
	}
	node->val->setConstValue(base->isConstValue());

	return true;
}

Value * IRGenerator::emitRValue(Value * value, const std::string & name)
{
	if (value == nullptr) {
		return nullptr;
	}

	if (dynamic_cast<ConstInt *>(value) != nullptr || dynamic_cast<ConstFloat *>(value) != nullptr) {
		return value;
	}

	if (isArrayObjectAddress(value)) {
		return decayArrayToPointer(value, name.empty() ? "arraydecay" : name);
	}

	if (auto * global = dynamic_cast<GlobalVariable *>(value); global != nullptr) {
		if (global->getType()->isArrayType()) {
			return decayArrayToPointer(value, name.empty() ? "arraydecay" : name);
		}
		return builder.createLoad(value, name);
	}

	if (dynamic_cast<PointerType *>(value->getType()) != nullptr) {
		return builder.createLoad(value, name);
	}

	return value;
}

Value * IRGenerator::convertValueToType(Value * value, Type * targetType, const std::string & name)
{
	if (value == nullptr || targetType == nullptr) {
		return nullptr;
	}

	Type * sourceType = value->getType();
	if (sourceType == targetType || sourceType->toString() == targetType->toString()) {
		return value;
	}

	if (sourceType->isInt1Byte() && targetType->isInt32Type()) {
		return builder.createZExt(value, targetType, name.empty() ? "booltoint" : name);
	}

	if (sourceType->isInt1Byte() && targetType->isFloatType()) {
		Value * intValue = builder.createZExt(value, IntegerType::getTypeInt(), "booltoint");
		return builder.createSIToFP(intValue, targetType, name.empty() ? "sitofp" : name);
	}

	if (sourceType->isIntegerType() && targetType->isFloatType()) {
		return builder.createSIToFP(value, targetType, name.empty() ? "sitofp" : name);
	}

	if (sourceType->isFloatType() && targetType->isInt32Type()) {
		return builder.createFPToSI(value, targetType, name.empty() ? "fptosi" : name);
	}

	return nullptr;
}

Value * IRGenerator::zeroValueForType(Type * type)
{
	if (type != nullptr && type->isFloatType()) {
		return module->newConstFloat(0.0f);
	}
	return module->newConstInt(0);
}

Value * IRGenerator::emitCondValue(Value * value)
{
	Value * cond = emitRValue(value, "cond");
	if (cond == nullptr) {
		return nullptr;
	}

	if (cond->getType()->isInt1Byte()) {
		return cond;
	}

	if (cond->getType()->isFloatType()) {
		return builder.createFCmpONE(cond, module->newConstFloat(0.0f), "tobool");
	}

	return builder.createICmpNE(cond, module->newConstInt(0), "tobool");
}

AllocaInst * IRGenerator::createEntryAlloca(Function * func, Type * type, const std::string & name)
{
	BasicBlock * entry = func->getEntryBlock();
	if (entry == nullptr) {
		entry = func->createBlock("entry");
	}

	auto * alloca = new AllocaInst(func, type, "");
	if (!name.empty()) {
		alloca->setName(name);
	}
	alloca->setIRName(func->allocateLocalName(name.empty() ? "" : name + ".addr"));

	auto & instructions = entry->getInstructions();
	auto insertPos = instructions.begin();
	while (insertPos != instructions.end() && dynamic_cast<AllocaInst *>(*insertPos) != nullptr) {
		++insertPos;
	}
	instructions.insert(insertPos, alloca);
	return alloca;
}

ast_node * IRGenerator::getDeclDimsNode(ast_node * node) const
{
	if (node == nullptr || node->sons.size() < 3) {
		return nullptr;
	}
	return node->sons[2]->node_type == ast_operator_type::AST_OP_ARRAY_DIMS ? node->sons[2] : nullptr;
}

ast_node * IRGenerator::getDeclInitNode(ast_node * node) const
{
	if (node == nullptr || node->sons.size() < 3) {
		return nullptr;
	}

	ast_node * dimsNode = getDeclDimsNode(node);
	size_t initIndex = dimsNode != nullptr ? 3 : 2;
	return node->sons.size() > initIndex ? node->sons[initIndex] : nullptr;
}

Type * IRGenerator::buildArrayType(Type * baseType, const std::vector<int32_t> & dims) const
{
	Type * type = baseType;
	for (auto iter = dims.rbegin(); iter != dims.rend(); ++iter) {
		type = new ArrayType(type, *iter);
	}
	return type;
}

bool IRGenerator::buildArrayTypeFromDims(
	ast_node * dimsNode, Type * baseType, Type *& resultType, std::vector<int32_t> * dims)
{
	resultType = baseType;
	if (dimsNode == nullptr) {
		return true;
	}

	std::vector<int32_t> dimValues;
	dimValues.reserve(dimsNode->sons.size());
	for (auto * dimNode: dimsNode->sons) {
		int32_t dimValue = 0;
		if (!eval_global_const_expr(dimNode, dimValue) || dimValue <= 0) {
			report_ir_error(
				"E1306",
				dimNode != nullptr ? dimNode->line_no : (dimsNode != nullptr ? dimsNode->line_no : -1),
				"数组声明",
				"数组维度必须是正整数常量表达式");
			return false;
		}
		dimValues.push_back(dimValue);
	}

	resultType = buildArrayType(baseType, dimValues);
	if (dims != nullptr) {
		*dims = dimValues;
	}
	return true;
}

Type * IRGenerator::buildFormalParamType(ast_node * paramNode)
{
	if (paramNode == nullptr) {
		return nullptr;
	}

	ast_node * dimsNode = nullptr;
	if (!paramNode->sons.empty() && paramNode->sons[0]->node_type == ast_operator_type::AST_OP_ARRAY_DIMS) {
		dimsNode = paramNode->sons[0];
	}

	if (dimsNode == nullptr) {
		return paramNode->type;
	}

	Type * pointeeType = paramNode->type;
	if (!dimsNode->sons.empty()) {
		Type * arrayType = nullptr;
		if (!buildArrayTypeFromDims(dimsNode, paramNode->type, arrayType)) {
			return nullptr;
		}
		pointeeType = arrayType;
	}

	return const_cast<PointerType *>(PointerType::get(pointeeType));
}

bool IRGenerator::isArrayObjectAddress(Value * value) const
{
	if (value == nullptr) {
		return false;
	}

	if (auto * global = dynamic_cast<GlobalVariable *>(value); global != nullptr) {
		return global->getType()->isArrayType();
	}

	if (auto * alloca = dynamic_cast<AllocaInst *>(value); alloca != nullptr) {
		return alloca->getAllocatedType() != nullptr && alloca->getAllocatedType()->isArrayType();
	}

	auto * ptrType = dynamic_cast<PointerType *>(value->getType());
	return ptrType != nullptr && ptrType->getPointeeType() != nullptr && ptrType->getPointeeType()->isArrayType();
}

Value * IRGenerator::decayArrayToPointer(Value * value, const std::string & name)
{
	if (value == nullptr) {
		return nullptr;
	}

	auto * zero = module->newConstInt(0);
	return builder.createGEP(value, {zero, zero}, name.empty() ? "arraydecay" : name);
}

size_t IRGenerator::getFlattenElementCount(Type * type) const
{
	if (auto * arrayType = dynamic_cast<ArrayType *>(type); arrayType != nullptr) {
		return static_cast<size_t>(arrayType->getElementCount()) * getFlattenElementCount(arrayType->getElementType());
	}
	return 1;
}

std::vector<int32_t> IRGenerator::getArrayDimensions(Type * arrayType) const
{
	std::vector<int32_t> dims;
	Type * current = arrayType;
	while (auto * currentArray = dynamic_cast<ArrayType *>(current)) {
		dims.push_back(currentArray->getElementCount());
		current = currentArray->getElementType();
	}
	return dims;
}

std::vector<int32_t> IRGenerator::flattenIndexToIndices(size_t flatIndex, const std::vector<int32_t> & dims) const
{
	std::vector<int32_t> indices(dims.size(), 0);
	for (size_t index = dims.size(); index > 0; --index) {
		int32_t dim = dims[index - 1];
		indices[index - 1] = static_cast<int32_t>(flatIndex % static_cast<size_t>(dim));
		flatIndex /= static_cast<size_t>(dim);
	}
	return indices;
}

bool IRGenerator::fillArrayInitializer(ast_node * initNode, Type * type, std::vector<ast_node *> & slots, size_t baseIndex)
{
	if (initNode == nullptr) {
		return true;
	}

	auto * arrayType = dynamic_cast<ArrayType *>(type);
	if (arrayType == nullptr) {
		if (initNode->node_type == ast_operator_type::AST_OP_INIT_LIST) {
			if (!initNode->sons.empty()) {
				slots[baseIndex] = initNode->sons[0];
			}
		} else {
			slots[baseIndex] = initNode;
		}
		return true;
	}

	if (initNode->node_type != ast_operator_type::AST_OP_INIT_LIST) {
		slots[baseIndex] = initNode;
		return true;
	}

	Type * elementType = arrayType->getElementType();
	const size_t elementSize = getFlattenElementCount(elementType);
	const size_t totalSize = getFlattenElementCount(arrayType);
	size_t offset = 0;

	for (auto * child: initNode->sons) {
		if (offset >= totalSize) {
			break;
		}

		if (child->node_type == ast_operator_type::AST_OP_INIT_LIST) {
			if (!fillArrayInitializer(child, elementType, slots, baseIndex + offset)) {
				return false;
			}
			offset += elementSize;
		} else {
			slots[baseIndex + offset] = child;
			++offset;
		}
	}

	return true;
}

bool IRGenerator::emitArrayInitializerStores(Value * arrayAddr, Type * arrayType, ast_node * initNode)
{
	const size_t total = getFlattenElementCount(arrayType);
	std::vector<ast_node *> slots(total, nullptr);
	if (!fillArrayInitializer(initNode, arrayType, slots, 0)) {
		return false;
	}

	std::vector<int32_t> dims = getArrayDimensions(arrayType);
	for (size_t flatIndex = 0; flatIndex < total; ++flatIndex) {
		std::vector<Value *> indices;
		indices.push_back(module->newConstInt(0));
		for (int32_t index: flattenIndexToIndices(flatIndex, dims)) {
			indices.push_back(module->newConstInt(index));
		}

		Value * elemAddr = builder.createGEP(arrayAddr, indices, "arrayinit.gep");
		if (elemAddr == nullptr) {
			return false;
		}

		Type * elementType = nullptr;
		if (auto * ptrType = dynamic_cast<PointerType *>(elemAddr->getType()); ptrType != nullptr) {
			elementType = const_cast<Type *>(ptrType->getPointeeType());
		}

		Value * initValue = zeroValueForType(elementType);
		if (slots[flatIndex] != nullptr) {
			ast_node * initAst = ir_visit_ast_node(slots[flatIndex]);
			if (initAst == nullptr) {
				return false;
			}
			initValue = emitRValue(initAst->val, "arrayinit");
			if (elementType != nullptr) {
				initValue = convertValueToType(initValue, elementType, "arrayinit.cast");
				if (initValue == nullptr) {
					return false;
				}
			}
		}

		builder.createStore(initValue, elemAddr);
	}

	return true;
}

bool IRGenerator::buildScalarInitializerText(Type * type, ast_node * initNode, std::string & initializerText)
{
	if (type != nullptr && type->isFloatType()) {
		float value = 0.0f;
		if (initNode != nullptr && !eval_global_const_float(initNode, value)) {
			return false;
		}
		initializerText = module->newConstFloat(value)->getIRName();
		return true;
	}

	int32_t value = 0;
	if (initNode != nullptr && !eval_global_const_expr(initNode, value)) {
		return false;
	}
	initializerText = std::to_string(value);
	return true;
}

bool IRGenerator::buildGlobalArrayInitializer(Type * arrayType, ast_node * initNode, std::string & initializerText)
{
	if (initNode == nullptr) {
		initializerText = "zeroinitializer";
		return true;
	}

	const size_t total = getFlattenElementCount(arrayType);
	std::vector<ast_node *> slots(total, nullptr);
	if (!fillArrayInitializer(initNode, arrayType, slots, 0)) {
		return false;
	}

	std::vector<std::string> values(total);
	for (size_t index = 0; index < total; ++index) {
		Type * currentType = arrayType;
		for (int32_t dimIndex: flattenIndexToIndices(index, getArrayDimensions(arrayType))) {
			(void) dimIndex;
			auto * currentArray = dynamic_cast<ArrayType *>(currentType);
			if (currentArray != nullptr) {
				currentType = currentArray->getElementType();
			}
		}
		(void) buildScalarInitializerText(currentType, nullptr, values[index]);
		if (slots[index] == nullptr) {
			continue;
		}
		if (!buildScalarInitializerText(currentType, slots[index], values[index])) {
			report_ir_error(
				"E1301",
				slots[index] != nullptr ? slots[index]->line_no : -1,
				"全局初始化",
				"全局数组初始化目前只支持常量表达式");
			return false;
		}
	}

	std::vector<int32_t> dims = getArrayDimensions(arrayType);
	size_t cursor = 0;
	auto emitAggregate = [&](auto && self, Type * currentType, bool includeType) -> std::string {
		if (auto * currentArray = dynamic_cast<ArrayType *>(currentType); currentArray != nullptr) {
			std::string text = includeType ? currentArray->toString() + " [" : "[";
			for (int32_t index = 0; index < currentArray->getElementCount(); ++index) {
				if (index != 0) {
					text += ", ";
				}
				text += self(self, currentArray->getElementType(), true);
			}
			text += "]";
			return text;
		}

		std::string value = cursor < values.size() ? values[cursor++] : "0";
		return includeType ? currentType->toString() + " " + value : value;
	};

	initializerText = emitAggregate(emitAggregate, arrayType, false);
	return true;
}

bool IRGenerator::ir_binary(ast_node * node, BinaryEmitOp op)
{
	// 二元表达式统一采用“先算左、再算右、最后生成结果指令”的顺序
	ast_node * leftNode = node->sons[0];
	ast_node * rightNode = node->sons[1];

	ast_node * left = ir_visit_ast_node(leftNode);
	if (!left) {
		return false;
	}

	ast_node * right = ir_visit_ast_node(rightNode);
	if (!right) {
		return false;
	}

	Value * lhs = emitRValue(left->val, "lhs");
	Value * rhs = emitRValue(right->val, "rhs");
	const bool useFloat = (lhs != nullptr && lhs->getType()->isFloatType()) || (rhs != nullptr && rhs->getType()->isFloatType());

	if (useFloat) {
		lhs = convertValueToType(lhs, FloatType::getTypeFloat(), "lhs.fp");
		rhs = convertValueToType(rhs, FloatType::getTypeFloat(), "rhs.fp");
		if (lhs == nullptr || rhs == nullptr) {
			report_ir_error("E1217", node != nullptr ? node->line_no : -1, "类型转换", "浮点二元表达式类型转换失败");
			return false;
		}
	}

	switch (op) {
		case BinaryEmitOp::Add:
			node->val = useFloat ? builder.createFAdd(lhs, rhs, "addtmp") : builder.createAdd(lhs, rhs, "addtmp");
			break;
		case BinaryEmitOp::Sub:
			node->val = useFloat ? builder.createFSub(lhs, rhs, "subtmp") : builder.createSub(lhs, rhs, "subtmp");
			break;
		case BinaryEmitOp::Mul:
			node->val = useFloat ? builder.createFMul(lhs, rhs, "multmp") : builder.createMul(lhs, rhs, "multmp");
			break;
		case BinaryEmitOp::Div:
			node->val = useFloat ? builder.createFDiv(lhs, rhs, "divtmp") : builder.createSDiv(lhs, rhs, "divtmp");
			break;
		case BinaryEmitOp::Mod:
			if (useFloat) {
				report_ir_error("E1218", node != nullptr ? node->line_no : -1, "类型检查", "float类型不支持取模运算");
				return false;
			}
			node->val = builder.createSRem(lhs, rhs, "modtmp");
			break;
		case BinaryEmitOp::Eq:
			node->val = builder.createZExt(
				useFloat ? static_cast<Value *>(builder.createFCmpOEQ(lhs, rhs, "cmptmp")) :
						   static_cast<Value *>(builder.createICmpEQ(lhs, rhs, "cmptmp")),
				IntegerType::getTypeInt(),
				"booltoint");
			break;
		case BinaryEmitOp::Ne:
			node->val = builder.createZExt(
				useFloat ? static_cast<Value *>(builder.createFCmpONE(lhs, rhs, "cmptmp")) :
						   static_cast<Value *>(builder.createICmpNE(lhs, rhs, "cmptmp")),
				IntegerType::getTypeInt(),
				"booltoint");
			break;
		case BinaryEmitOp::Lt:
			node->val = builder.createZExt(
				useFloat ? static_cast<Value *>(builder.createFCmpOLT(lhs, rhs, "cmptmp")) :
						   static_cast<Value *>(builder.createICmpSLT(lhs, rhs, "cmptmp")),
				IntegerType::getTypeInt(),
				"booltoint");
			break;
		case BinaryEmitOp::Le:
			node->val = builder.createZExt(
				useFloat ? static_cast<Value *>(builder.createFCmpOLE(lhs, rhs, "cmptmp")) :
						   static_cast<Value *>(builder.createICmpSLE(lhs, rhs, "cmptmp")),
				IntegerType::getTypeInt(),
				"booltoint");
			break;
		case BinaryEmitOp::Gt:
			node->val = builder.createZExt(
				useFloat ? static_cast<Value *>(builder.createFCmpOGT(lhs, rhs, "cmptmp")) :
						   static_cast<Value *>(builder.createICmpSGT(lhs, rhs, "cmptmp")),
				IntegerType::getTypeInt(),
				"booltoint");
			break;
		case BinaryEmitOp::Ge:
			node->val = builder.createZExt(
				useFloat ? static_cast<Value *>(builder.createFCmpOGE(lhs, rhs, "cmptmp")) :
						   static_cast<Value *>(builder.createICmpSGE(lhs, rhs, "cmptmp")),
				IntegerType::getTypeInt(),
				"booltoint");
			break;
		default:
			return false;
	}

	return true;
}

// 整数加法AST节点翻译成线性中间IR
bool IRGenerator::ir_add(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Add);
}

// 整数减法AST节点翻译成线性中间IR
bool IRGenerator::ir_sub(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Sub);
}

// 整数乘法AST节点翻译成线性中间IR
bool IRGenerator::ir_mul(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Mul);
}

// 整数除法AST节点翻译成线性中间IR
bool IRGenerator::ir_div(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Div);
}

// 整数求余AST节点翻译成线性中间IR
bool IRGenerator::ir_mod(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Mod);
}

// 整数相等比较AST节点翻译成线性中间IR
bool IRGenerator::ir_eq(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Eq);
}

// 整数不等比较AST节点翻译成线性中间IR
bool IRGenerator::ir_ne(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Ne);
}

// 整数小于比较AST节点翻译成线性中间IR
bool IRGenerator::ir_lt(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Lt);
}

// 整数小于等于比较AST节点翻译成线性中间IR
bool IRGenerator::ir_le(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Le);
}

// 整数大于比较AST节点翻译成线性中间IR
bool IRGenerator::ir_gt(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Gt);
}

// 整数大于等于比较AST节点翻译成线性中间IR
bool IRGenerator::ir_ge(ast_node * node)
{
	return ir_binary(node, BinaryEmitOp::Ge);
}

// 逻辑非AST节点翻译成线性中间IR
bool IRGenerator::ir_logical_not(ast_node * node)
{
	ast_node * exprNode = node->sons[0];
	ast_node * expr = ir_visit_ast_node(exprNode);
	if (!expr) {
		return false;
	}

	// 逻辑非统一翻译成“expr == 0”，结果仍为i32 0/1
	Value * value = emitRValue(expr->val, "nottmp");
	Value * cmp = value->getType()->isFloatType() ? static_cast<Value *>(builder.createFCmpOEQ(value, module->newConstFloat(0.0f), "nottmp")) :
													static_cast<Value *>(builder.createICmpEQ(value, module->newConstInt(0), "nottmp"));
	node->val = builder.createZExt(cmp, IntegerType::getTypeInt(), "booltoint");

	return true;
}

// 逻辑与AST节点翻译成线性中间IR，按短路语义生成控制流
bool IRGenerator::ir_logical_and(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	AllocaInst * result = createEntryAlloca(currentFunc, IntegerType::getTypeInt(), "and.tmp");
	if (result == nullptr) {
		report_ir_error("E1215", node != nullptr ? node->line_no : -1, "逻辑运算", "逻辑与结果临时变量创建失败");
		return false;
	}

	ast_node * left = ir_visit_ast_node(node->sons[0]);
	if (!left) {
		return false;
	}

	BasicBlock * rhsBlock = currentFunc->createBlock("land.rhs");
	BasicBlock * trueBlock = currentFunc->createBlock("land.true");
	BasicBlock * endBlock = currentFunc->createBlock("land.end");

	builder.createStore(module->newConstInt(0), result);
	// 左侧为假直接结束，为真才继续计算右侧
	builder.createCondBr(emitCondValue(left->val), rhsBlock, endBlock);
	builder.setInsertPoint(rhsBlock);

	ast_node * right = ir_visit_ast_node(node->sons[1]);
	if (!right) {
		return false;
	}

	builder.createCondBr(emitCondValue(right->val), trueBlock, endBlock);
	builder.setInsertPoint(trueBlock);
	builder.createStore(module->newConstInt(1), result);
	builder.createBr(endBlock);
	builder.setInsertPoint(endBlock);
	node->val = builder.createLoad(result, "land.val");

	return true;
}

// 逻辑或AST节点翻译成线性中间IR，按短路语义生成控制流
bool IRGenerator::ir_logical_or(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	AllocaInst * result = createEntryAlloca(currentFunc, IntegerType::getTypeInt(), "or.tmp");
	if (result == nullptr) {
		report_ir_error("E1216", node != nullptr ? node->line_no : -1, "逻辑运算", "逻辑或结果临时变量创建失败");
		return false;
	}

	ast_node * left = ir_visit_ast_node(node->sons[0]);
	if (!left) {
		return false;
	}

	BasicBlock * rhsBlock = currentFunc->createBlock("lor.rhs");
	BasicBlock * falseBlock = currentFunc->createBlock("lor.false");
	BasicBlock * endBlock = currentFunc->createBlock("lor.end");

	builder.createStore(module->newConstInt(1), result);
	// 左侧为真直接结束，为假才继续计算右侧
	builder.createCondBr(emitCondValue(left->val), endBlock, rhsBlock);
	builder.setInsertPoint(rhsBlock);

	ast_node * right = ir_visit_ast_node(node->sons[1]);
	if (!right) {
		return false;
	}

	builder.createCondBr(emitCondValue(right->val), endBlock, falseBlock);
	builder.setInsertPoint(falseBlock);
	builder.createStore(module->newConstInt(0), result);
	builder.createBr(endBlock);
	builder.setInsertPoint(endBlock);
	node->val = builder.createLoad(result, "lor.val");

	return true;
}

// if语句AST节点翻译成线性中间IR
bool IRGenerator::ir_if(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	ast_node * condNode = ir_visit_ast_node(node->sons[0]);
	if (!condNode) {
		return false;
	}

	BasicBlock * thenBlock = currentFunc->createBlock("if.then");
	BasicBlock * endBlock = currentFunc->createBlock("if.end");
	BasicBlock * elseBlock = endBlock;
	if (node->sons.size() > 2 && node->sons[2] != nullptr) {
		elseBlock = currentFunc->createBlock("if.else");
	}

	builder.createCondBr(emitCondValue(condNode->val), thenBlock, elseBlock);
	builder.setInsertPoint(thenBlock);

	ast_node * thenNode = ir_visit_ast_node(node->sons[1]);
	if (!thenNode) {
		return false;
	}

	if (builder.getInsertBlock() != nullptr && !builder.getInsertBlock()->hasTerminator()) {
		builder.createBr(endBlock);
	}

	if (elseBlock != endBlock) {
		builder.setInsertPoint(elseBlock);
		ast_node * elseNode = ir_visit_ast_node(node->sons[2]);
		if (!elseNode) {
			return false;
		}

		if (builder.getInsertBlock() != nullptr && !builder.getInsertBlock()->hasTerminator()) {
			builder.createBr(endBlock);
		}
	}

	builder.setInsertPoint(endBlock);
	return true;
}

// while语句AST节点翻译成线性中间IR
bool IRGenerator::ir_while(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	BasicBlock * condBlock = currentFunc->createBlock("while.cond");
	BasicBlock * bodyBlock = currentFunc->createBlock("while.body");
	BasicBlock * endBlock = currentFunc->createBlock("while.end");

	// 记录当前循环的continue/break目标，供嵌套循环中的break/continue查栈顶
	loopTargets.emplace_back(condBlock, endBlock);

	builder.createBr(condBlock);
	builder.setInsertPoint(condBlock);

	ast_node * condNode = ir_visit_ast_node(node->sons[0]);
	if (!condNode) {
		loopTargets.pop_back();
		return false;
	}

	builder.createCondBr(emitCondValue(condNode->val), bodyBlock, endBlock);
	builder.setInsertPoint(bodyBlock);

	ast_node * bodyNode = ir_visit_ast_node(node->sons[1]);
	if (!bodyNode) {
		loopTargets.pop_back();
		return false;
	}

	if (builder.getInsertBlock() != nullptr && !builder.getInsertBlock()->hasTerminator()) {
		builder.createBr(condBlock);
	}

	loopTargets.pop_back();
	builder.setInsertPoint(endBlock);

	return true;
}

// for语句AST节点翻译成MiniLLVM IR
bool IRGenerator::ir_for(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	if (currentFunc == nullptr) {
		report_ir_error("E1212", node != nullptr ? node->line_no : -1, "控制流检查", "for语句不在函数内");
		return false;
	}

	if (node == nullptr || node->sons.size() < 4) {
		report_ir_error("E1213", node != nullptr ? node->line_no : -1, "控制流检查", "for节点结构非法");
		return false;
	}

	ast_node * initNode = node->sons[0];
	ast_node * condExprNode = node->sons[1];
	ast_node * stepNode = node->sons[2];
	ast_node * bodyNode = node->sons[3];

	if (!ir_visit_ast_node(initNode)) {
		return false;
	}

	BasicBlock * condBlock = currentFunc->createBlock("for.cond");
	BasicBlock * bodyBlock = currentFunc->createBlock("for.body");
	BasicBlock * stepBlock = currentFunc->createBlock("for.step");
	BasicBlock * endBlock = currentFunc->createBlock("for.end");

	builder.createBr(condBlock);
	builder.setInsertPoint(condBlock);

	ast_node * condNode = ir_visit_ast_node(condExprNode);
	if (!condNode) {
		return false;
	}

	builder.createCondBr(emitCondValue(condNode->val), bodyBlock, endBlock);
	builder.setInsertPoint(bodyBlock);

	// for的continue目标是步进块，break目标是循环结束块
	loopTargets.emplace_back(stepBlock, endBlock);
	ast_node * translatedBody = ir_visit_ast_node(bodyNode);
	if (!translatedBody) {
		loopTargets.pop_back();
		return false;
	}
	loopTargets.pop_back();

	if (builder.getInsertBlock() != nullptr && !builder.getInsertBlock()->hasTerminator()) {
		builder.createBr(stepBlock);
	}

	builder.setInsertPoint(stepBlock);
	ast_node * translatedStep = ir_visit_ast_node(stepNode);
	if (!translatedStep) {
		return false;
	}

	if (builder.getInsertBlock() != nullptr && !builder.getInsertBlock()->hasTerminator()) {
		builder.createBr(condBlock);
	}

	builder.setInsertPoint(endBlock);
	return true;
}

// break语句AST节点翻译成线性中间IR
bool IRGenerator::ir_break(ast_node * node)
{
	if (loopTargets.empty()) {
		report_ir_error("E1210", node != nullptr ? node->line_no : -1, "控制流检查", "break不在循环内");
		return false;
	}

	builder.createBr(loopTargets.back().second);
	return true;
}

// continue语句AST节点翻译成线性中间IR
bool IRGenerator::ir_continue(ast_node * node)
{
	if (loopTargets.empty()) {
		report_ir_error("E1211", node != nullptr ? node->line_no : -1, "控制流检查", "continue不在循环内");
		return false;
	}

	builder.createBr(loopTargets.back().first);
	return true;
}

// 赋值AST节点翻译成线性中间IR
bool IRGenerator::ir_assign(ast_node * node)
{
	ast_node * son1_node = node->sons[0];
	ast_node * son2_node = node->sons[1];

	// 赋值节点，自右往左运算

	// 赋值运算符的左侧操作数
	ast_node * left = ir_visit_ast_node(son1_node);
	if (!left) {
		return false;
	}

	if (left->val == nullptr) {
		report_ir_error(
			"E1305",
			son1_node != nullptr ? son1_node->line_no : (node != nullptr ? node->line_no : -1),
			"赋值语句",
			"赋值左值无效");
		return false;
	}

	if (left->val->isConstValue()) {
		report_ir_error(
			"E1303",
			son1_node != nullptr ? son1_node->line_no : (node != nullptr ? node->line_no : -1),
			"常量赋值",
			"常量(%s)不允许重新赋值",
			son1_node != nullptr ? son1_node->name.c_str() : "<unknown>");
		return false;
	}

	// 赋值运算符的右侧操作数
	ast_node * right = ir_visit_ast_node(son2_node);
	if (!right) {
		return false;
	}

	Type * targetType = nullptr;
	if (auto * ptrType = dynamic_cast<PointerType *>(left->val->getType()); ptrType != nullptr) {
		targetType = const_cast<Type *>(ptrType->getPointeeType());
	} else if (dynamic_cast<GlobalVariable *>(left->val) != nullptr) {
		targetType = left->val->getType();
	}

	Value * value = emitRValue(right->val, "assigntmp");
	if (targetType != nullptr) {
		value = convertValueToType(value, targetType, "assigncast");
		if (value == nullptr) {
			report_ir_error("E1308", node != nullptr ? node->line_no : -1, "赋值语句", "赋值类型转换失败");
			return false;
		}
	}
	builder.createStore(value, left->val);
	node->val = value;

	return true;
}

// 变量声明语句节点翻译成线性中间IR
bool IRGenerator::ir_declare_statment(ast_node * node)
{
	bool result = false;

	for (auto & child: node->sons) {

		// 遍历每个变量声明
		result = ir_visit_ast_node(child) != nullptr;
		if (!result) {
			break;
		}

	}

	return result;
}

// 变量定声明节点翻译成线性中间IR
bool IRGenerator::ir_variable_declare(ast_node * node)
{
	// 共有两个孩子，第一个类型，第二个变量名

	// TODO 这里可强化类型等检查
	if (node == nullptr || node->sons.size() < 2) {
		report_ir_error("E1300", node != nullptr ? node->line_no : -1, "变量声明", "变量声明节点非法");
		return false;
	}

	if (node->isConst && node->sons.size() < 3) {
		report_ir_error(
			"E1304",
			node->sons[1] != nullptr ? node->sons[1]->line_no : -1,
			"常量声明",
			"常量(%s)必须初始化",
			node->sons[1]->name.c_str());
		return false;
	}

	ast_node * dimsNode = getDeclDimsNode(node);
	ast_node * initExpr = getDeclInitNode(node);
	Type * declaredType = node->sons[0]->type;
	std::vector<int32_t> arrayDims;
	if (dimsNode != nullptr && !buildArrayTypeFromDims(dimsNode, node->sons[0]->type, declaredType, &arrayDims)) {
		return false;
	}
	node->type = declaredType;

	if (module->getCurrentFunction() == nullptr) {
		node->val = module->newVarValue(declaredType, node->sons[1]->name, node->sons[1]->line_no);
		if (node->val == nullptr) {
			return false;
		}
		node->val->setConstValue(node->isConst);

		auto * global_var = dynamic_cast<GlobalVariable *>(node->val);
		if (global_var == nullptr) {
			report_ir_error(
				"E1302",
				node->sons[1] != nullptr ? node->sons[1]->line_no : -1,
				"全局初始化",
				"全局变量(%s)初始化失败",
				node->sons[1]->name.c_str());
			return false;
		}

		if (dimsNode != nullptr) {
			std::string initializerText;
			if (!buildGlobalArrayInitializer(declaredType, initExpr, initializerText)) {
				return false;
			}
			global_var->setInitializerText(initializerText);
			return true;
		}

		if (initExpr == nullptr) {
			return true;
		}

		if (declaredType->isFloatType()) {
			std::string initializerText;
			if (!buildScalarInitializerText(declaredType, initExpr, initializerText)) {
				report_ir_error(
					"E1301",
					node->sons[1] != nullptr ? node->sons[1]->line_no : -1,
					"全局初始化",
					"全局变量(%s)初始化目前只支持常量表达式",
					node->sons[1]->name.c_str());
				return false;
			}
			global_var->setInitializerText(initializerText);
			if (node->isConst) {
				float constValue = 0.0f;
				if (eval_global_const_float(initExpr, constValue)) {
					node->val->setConstFloatValue(constValue);
				}
			}
			return true;
		}

		int32_t init_value = 0;
		if (!eval_global_const_expr(initExpr, init_value)) {
			report_ir_error(
				"E1301",
				node->sons[1] != nullptr ? node->sons[1]->line_no : -1,
				"全局初始化",
				"全局变量(%s)初始化目前只支持常量整数表达式",
				node->sons[1]->name.c_str());
			return false;
		}

		global_var->setInitializer(init_value);
		if (node->isConst) {
			node->val->setConstIntValue(init_value);
		}
		return true;
	}

	AllocaInst * alloca = createEntryAlloca(module->getCurrentFunction(), declaredType, node->sons[1]->name);
	if (alloca == nullptr || !module->bindValue(node->sons[1]->name, alloca, node->sons[1]->line_no)) {
		return false;
	}
	node->val = alloca;
	node->val->setConstValue(node->isConst);

	if (initExpr == nullptr) {
		return true;
	}

	if (dimsNode != nullptr) {
		return emitArrayInitializerStores(node->val, declaredType, initExpr);
	}

	ast_node * init_node = ir_visit_ast_node(initExpr);
	if (!init_node) {
		return false;
	}

	Value * initValue = emitRValue(init_node->val, "inittmp");
	initValue = convertValueToType(initValue, declaredType, "initcast");
	if (initValue == nullptr) {
		report_ir_error("E1309", node != nullptr ? node->line_no : -1, "变量声明", "初始化类型转换失败");
		return false;
	}
	builder.createStore(initValue, node->val);
	if (node->isConst) {
		int32_t constValue = 0;
		if (eval_global_const_expr(initExpr, constValue)) {
			node->val->setConstIntValue(constValue);
		}
		float constFloat = 0.0f;
		if (eval_global_const_float(initExpr, constFloat)) {
			node->val->setConstFloatValue(constFloat);
		}
	}

	return true;
}

// 常量声明节点翻译成MiniLLVM IR
bool IRGenerator::ir_const_declaration(ast_node * node)
{
	if (node != nullptr) {
		node->isConst = true;
	}
	return ir_variable_declare(node);
}

bool IRGenerator::eval_global_const_expr(ast_node * node, int32_t & value)
{
	if (node == nullptr) {
		return false;
	}

	// 全局初始化要求在编译期可折叠，这里只接受当前子集支持的整型常量表达式
	switch (node->node_type) {
		case ast_operator_type::AST_OP_LEAF_LITERAL_UINT:
			value = static_cast<int32_t>(node->integer_val);
			return true;

		case ast_operator_type::AST_OP_LEAF_LITERAL_FLOAT:
			value = static_cast<int32_t>(node->float_val);
			return true;

		case ast_operator_type::AST_OP_LEAF_VAR_ID: {
			Value * constValue = module->lookupValue(node->name);
			if (constValue == nullptr || !constValue->isConstValue()) {
				return false;
			}
			if (constValue->hasConstIntValue()) {
				value = constValue->getConstIntValue();
				return true;
			}
			if (constValue->hasConstFloatValue()) {
				value = static_cast<int32_t>(constValue->getConstFloatValue());
				return true;
			}
			return false;
		}

		case ast_operator_type::AST_OP_INIT_LIST:
			if (node->sons.empty()) {
				value = 0;
				return true;
			}
			return eval_global_const_expr(node->sons[0], value);

		case ast_operator_type::AST_OP_NOT: {
			if (node->sons.size() != 1) {
				return false;
			}

			int32_t operand = 0;
			if (!eval_global_const_expr(node->sons[0], operand)) {
				return false;
			}

			// 逻辑非的常量折叠结果仍按i32 0/1处理
			value = operand == 0 ? 1 : 0;
			return true;
		}

		case ast_operator_type::AST_OP_ADD:
		case ast_operator_type::AST_OP_SUB:
		case ast_operator_type::AST_OP_MUL:
		case ast_operator_type::AST_OP_DIV:
		case ast_operator_type::AST_OP_MOD:
		case ast_operator_type::AST_OP_EQ:
		case ast_operator_type::AST_OP_NE:
		case ast_operator_type::AST_OP_LT:
		case ast_operator_type::AST_OP_LE:
		case ast_operator_type::AST_OP_GT:
		case ast_operator_type::AST_OP_GE:
		case ast_operator_type::AST_OP_LAND:
		case ast_operator_type::AST_OP_LOR: {
			if (node->sons.size() != 2) {
				return false;
			}

			int32_t lhs = 0;
			int32_t rhs = 0;
			if (!eval_global_const_expr(node->sons[0], lhs) || !eval_global_const_expr(node->sons[1], rhs)) {
				return false;
			}

			// 关系和逻辑表达式的编译期求值结果同样统一为0/1
			switch (node->node_type) {
				case ast_operator_type::AST_OP_ADD:
					value = lhs + rhs;
					return true;
				case ast_operator_type::AST_OP_SUB:
					value = lhs - rhs;
					return true;
				case ast_operator_type::AST_OP_MUL:
					value = lhs * rhs;
					return true;
				case ast_operator_type::AST_OP_DIV:
					if (rhs == 0) {
						return false;
					}
					value = lhs / rhs;
					return true;
				case ast_operator_type::AST_OP_MOD:
					if (rhs == 0) {
						return false;
					}
					value = lhs % rhs;
					return true;
				case ast_operator_type::AST_OP_EQ:
					value = lhs == rhs ? 1 : 0;
					return true;
				case ast_operator_type::AST_OP_NE:
					value = lhs != rhs ? 1 : 0;
					return true;
				case ast_operator_type::AST_OP_LT:
					value = lhs < rhs ? 1 : 0;
					return true;
				case ast_operator_type::AST_OP_LE:
					value = lhs <= rhs ? 1 : 0;
					return true;
				case ast_operator_type::AST_OP_GT:
					value = lhs > rhs ? 1 : 0;
					return true;
				case ast_operator_type::AST_OP_GE:
					value = lhs >= rhs ? 1 : 0;
					return true;
				case ast_operator_type::AST_OP_LAND:
					value = (lhs != 0 && rhs != 0) ? 1 : 0;
					return true;
				case ast_operator_type::AST_OP_LOR:
					value = (lhs != 0 || rhs != 0) ? 1 : 0;
					return true;
				default:
					return false;
			}
		}

		default:
			return false;
	}
}

bool IRGenerator::eval_global_const_float(ast_node * node, float & value)
{
	if (node == nullptr) {
		return false;
	}

	switch (node->node_type) {
		case ast_operator_type::AST_OP_LEAF_LITERAL_UINT:
			value = static_cast<float>(node->integer_val);
			return true;

		case ast_operator_type::AST_OP_LEAF_LITERAL_FLOAT:
			value = node->float_val;
			return true;

		case ast_operator_type::AST_OP_LEAF_VAR_ID: {
			Value * constValue = module->lookupValue(node->name);
			if (constValue == nullptr || !constValue->isConstValue()) {
				return false;
			}
			if (constValue->hasConstFloatValue()) {
				value = constValue->getConstFloatValue();
				return true;
			}
			if (constValue->hasConstIntValue()) {
				value = static_cast<float>(constValue->getConstIntValue());
				return true;
			}
			return false;
		}

		case ast_operator_type::AST_OP_INIT_LIST:
			if (node->sons.empty()) {
				value = 0.0f;
				return true;
			}
			return eval_global_const_float(node->sons[0], value);

		case ast_operator_type::AST_OP_NOT: {
			if (node->sons.size() != 1) {
				return false;
			}
			float operand = 0.0f;
			if (!eval_global_const_float(node->sons[0], operand)) {
				return false;
			}
			value = operand == 0.0f ? 1.0f : 0.0f;
			return true;
		}

		case ast_operator_type::AST_OP_ADD:
		case ast_operator_type::AST_OP_SUB:
		case ast_operator_type::AST_OP_MUL:
		case ast_operator_type::AST_OP_DIV:
		case ast_operator_type::AST_OP_MOD:
		case ast_operator_type::AST_OP_EQ:
		case ast_operator_type::AST_OP_NE:
		case ast_operator_type::AST_OP_LT:
		case ast_operator_type::AST_OP_LE:
		case ast_operator_type::AST_OP_GT:
		case ast_operator_type::AST_OP_GE:
		case ast_operator_type::AST_OP_LAND:
		case ast_operator_type::AST_OP_LOR: {
			if (node->sons.size() != 2) {
				return false;
			}

			float lhs = 0.0f;
			float rhs = 0.0f;
			if (!eval_global_const_float(node->sons[0], lhs) || !eval_global_const_float(node->sons[1], rhs)) {
				return false;
			}

			switch (node->node_type) {
				case ast_operator_type::AST_OP_ADD:
					value = lhs + rhs;
					return true;
				case ast_operator_type::AST_OP_SUB:
					value = lhs - rhs;
					return true;
				case ast_operator_type::AST_OP_MUL:
					value = lhs * rhs;
					return true;
				case ast_operator_type::AST_OP_DIV:
					if (rhs == 0.0f) {
						return false;
					}
					value = lhs / rhs;
					return true;
				case ast_operator_type::AST_OP_MOD:
					if (rhs == 0.0f) {
						return false;
					}
					value = std::fmod(lhs, rhs);
					return true;
				case ast_operator_type::AST_OP_EQ:
					value = lhs == rhs ? 1.0f : 0.0f;
					return true;
				case ast_operator_type::AST_OP_NE:
					value = lhs != rhs ? 1.0f : 0.0f;
					return true;
				case ast_operator_type::AST_OP_LT:
					value = lhs < rhs ? 1.0f : 0.0f;
					return true;
				case ast_operator_type::AST_OP_LE:
					value = lhs <= rhs ? 1.0f : 0.0f;
					return true;
				case ast_operator_type::AST_OP_GT:
					value = lhs > rhs ? 1.0f : 0.0f;
					return true;
				case ast_operator_type::AST_OP_GE:
					value = lhs >= rhs ? 1.0f : 0.0f;
					return true;
				case ast_operator_type::AST_OP_LAND:
					value = (lhs != 0.0f && rhs != 0.0f) ? 1.0f : 0.0f;
					return true;
				case ast_operator_type::AST_OP_LOR:
					value = (lhs != 0.0f || rhs != 0.0f) ? 1.0f : 0.0f;
					return true;
				default:
					return false;
			}
		}

		default:
			return false;
	}
}

// 函数调用AST节点翻译成线性中间IR
bool IRGenerator::ir_function_call(ast_node * node)
{
	std::vector<Value *> realParams;

	std::string funcName = node->sons[0]->name;
	int64_t lineno = node->sons[0]->line_no;

	// 获取当前正在处理的函数
	Function * currentFunc = module->getCurrentFunction();
	if (currentFunc == nullptr) {
		report_ir_error("E1203", lineno, "语义检查", "函数调用(%s)只能出现在函数体内", funcName.c_str());
		return false;
	}

	// 函数调用的节点包含两个节点：
	// 第一个节点：函数名节点
	// 第二个节点：实参列表节点

	ast_node * paramsNode = node->sons[1];

	// 根据函数名查找函数，看是否存在。函数签名应已在编译单元阶段完成预声明
	auto calledFunction = module->findFunction(funcName);
	if (nullptr == calledFunction) {
		report_ir_error("E1200", lineno, "语义检查", "函数(%s)未定义或未声明", funcName.c_str());
		return false;
	}

	auto & formalParams = calledFunction->getParams();
	if (paramsNode->sons.size() != formalParams.size()) {
		report_ir_error(
			"E1201",
			lineno,
			"参数检查",
			"函数(%s)调用参数个数不匹配，期望%zu个，实际%zu个",
			funcName.c_str(),
			formalParams.size(),
			paramsNode->sons.size());
		return false;
	}

	// 如果没有孩子，也认为是没有参数
	if (!paramsNode->sons.empty()) {

		int32_t argsCount = (int32_t) paramsNode->sons.size();

		// 当前函数中调用函数实参个数最大值统计，实际上是统计实参传参需在栈中分配的大小
		// 因为目前的语言支持的int和float都是四字节的，只统计个数即可
		if (argsCount > currentFunc->getMaxFuncCallArgCnt()) {
			currentFunc->setMaxFuncCallArgCnt(argsCount);
		}

		// 遍历参数列表，孩子是表达式
		// 这里自左往右计算表达式
		for (size_t index = 0; index < paramsNode->sons.size(); ++index) {
			auto son = paramsNode->sons[index];

			// 遍历Block的每个语句，进行显示或者运算
			ast_node * temp = ir_visit_ast_node(son);
			if (!temp) {
				return false;
			}

			Value * realValue = emitRValue(temp->val, "arg");
			Type * realType = realValue != nullptr ? realValue->getType() : nullptr;
			Type * formalType = formalParams[index]->getType();
			if (realType != nullptr && formalType != nullptr && (formalType->isFloatType() || formalType->isInt32Type())) {
				realValue = convertValueToType(realValue, formalType, "argcast");
				realType = realValue != nullptr ? realValue->getType() : nullptr;
			}
			if (realType == nullptr || formalType == nullptr || realType->toString() != formalType->toString()) {
				report_ir_error(
					"E1202",
					lineno,
					"参数检查",
					"函数(%s)第%zu个参数类型不匹配，期望%s，实际%s",
					funcName.c_str(),
					index + 1,
					formalType != nullptr ? formalType->toString().c_str() : "<null>",
					realType != nullptr ? realType->toString().c_str() : "<null>");
				return false;
			}

			realParams.push_back(realValue);
		}
	}

	// 返回调用有返回值，则需要分配临时变量，用于保存函数调用的返回值
	Type * type = calledFunction->getReturnType();

	node->val = builder.createCall(calledFunction, realParams, type->isVoidType() ? "" : "calltmp");

	return true;
}
