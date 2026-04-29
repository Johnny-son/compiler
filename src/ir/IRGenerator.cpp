#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "IRGenerator.h"
#include "ir/include/Function.h"
#include "ir/include/Type.h"
#include "ir/include/Value.h"
#include "ir/Instructions/BinaryInstruction.h"
#include "ir/Instructions/CondBranchInstruction.h"
#include "ir/Instructions/EntryInstruction.h"
#include "ir/Instructions/ExitInstruction.h"
#include "ir/Instructions/FuncCallInstruction.h"
#include "ir/Instructions/GotoInstruction.h"
#include "ir/Instructions/LabelInstruction.h"
#include "ir/Instructions/MoveInstruction.h"
#include "ir/Types/IntegerType.h"
#include "ir/Values/ConstInt.h"
#include "ir/Values/FormalParam.h"
#include "ir/Values/LocalVariable.h"
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
IRGenerator::IRGenerator(ast_node * _root, Module * _module) : root(_root), module(_module)
{
	/* 叶子节点 */
	ast2ir_handlers[ast_operator_type::AST_OP_LEAF_LITERAL_UINT] = &IRGenerator::ir_leaf_node_uint;
	ast2ir_handlers[ast_operator_type::AST_OP_LEAF_VAR_ID] = &IRGenerator::ir_leaf_node_var_id;
	ast2ir_handlers[ast_operator_type::AST_OP_LEAF_TYPE] = &IRGenerator::ir_leaf_node_type;

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

// 遍历抽象语法树产生线性IR，保存到IRCode中
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

	// 第一阶段：先把所有函数签名注册进模块，支持先调用后定义和递归
	for (auto son: node->sons) {
		if (son != nullptr && son->node_type == ast_operator_type::AST_OP_FUNC_DEF) {
			if (!predeclare_function(son)) {
				return false;
			}
		}
	}

	// 第二阶段：翻译全局变量声明，初始化仍处于全局上下文
	for (auto son: node->sons) {
		if (son != nullptr && son->node_type == ast_operator_type::AST_OP_DECL_STMT) {
			ast_node * son_node = ir_visit_ast_node(son);
			if (!son_node) {
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
		Value * localParam = module->newVarValue(
			param->getType(),
			param->getName(),
			paramNode != nullptr ? paramNode->line_no : (node != nullptr ? node->line_no : -1)
		);
		if (localParam == nullptr) {
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
		node->blockInsts.addInst(new MoveInstruction(currentFunc, localParam, param));
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

		formalParams.push_back(new FormalParam(param->type, param->name));
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
		InterCode & irCode = currentFunc->getInterCode();

		// 函数入口指令放在最前，后端和输出阶段都依赖它建立函数框架
		irCode.addInst(new EntryInstruction(currentFunc));

		// 所有return最终都汇聚到统一出口标签，便于后端和文本输出处理
		auto * exitLabelInst = new LabelInstruction(currentFunc);
		currentFunc->setExitLabel(exitLabelInst);

		if (!ir_function_formal_params(param_node)) {
			break;
		}
		node->blockInsts.addInst(param_node->blockInsts);

		LocalVariable * retValue = nullptr;
		if (!type_node->type->isVoidType()) {
			// 非void函数统一分配返回值槽，return时先写槽再跳统一出口
			retValue = static_cast<LocalVariable *>(module->newVarValue(type_node->type, "", -1));
			if (retValue == nullptr) {
				report_ir_error(
					"E1105",
					name_node != nullptr ? name_node->line_no : -1,
					"返回值处理",
					"函数(%s)返回值变量创建失败",
					name_node->name.c_str());
				break;
			}
		}
		currentFunc->setReturnValue(retValue);

		// 函数体作用域已在进入函数时建立，因此函数体block本身不再重复enterScope
		block_node->needScope = false;
		if (!ir_block(block_node)) {
			break;
		}

		node->blockInsts.addInst(block_node->blockInsts);
		irCode.addInst(node->blockInsts);
		irCode.addInst(exitLabelInst);
		irCode.addInst(new ExitInstruction(currentFunc, retValue));
		ok = true;
	} while (false);

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

		// 遍历Block的每个语句，进行显示或者运算
		ast_node * temp = ir_visit_ast_node(*pIter);
		if (!temp) {
			return false;
		}

		node->blockInsts.addInst(temp->blockInsts);
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

	LocalVariable * retSlot = currentFunc->getReturnValue();
	if (right != nullptr && retSlot == nullptr) {
		report_ir_error("E1120", node != nullptr ? node->line_no : -1, "返回语句", "void函数不能返回表达式");
		return false;
	}

	if (right == nullptr && retSlot != nullptr) {
		report_ir_error("E1121", node != nullptr ? node->line_no : -1, "返回语句", "非void函数必须返回表达式");
		return false;
	}

	// 返回值存在时则移动指令到node中
	if (right) {

		// 创建临时变量保存IR的值，以及线性IR指令
		node->blockInsts.addInst(right->blockInsts);

		// 返回值赋值到函数返回值变量上，然后跳转到函数的尾部
		node->blockInsts.addInst(new MoveInstruction(currentFunc, retSlot, right->val));

		node->val = right->val;
	} else {
		// 没有返回值
		node->val = nullptr;
	}

	// 跳转到函数的尾部出口指令上
	node->blockInsts.addInst(new GotoInstruction(currentFunc, currentFunc->getExitLabel()));

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

// 标识符叶子节点翻译成线性中间IR，变量声明的不走这个语句
bool IRGenerator::ir_leaf_node_var_id(ast_node * node)
{
	Value * val;

	// 查找ID型Value
	// 变量，则需要在符号表中查找对应的值

	val = module->findVarValue(node->name);
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

bool IRGenerator::ir_binary(ast_node * node, IRInstOperator op)
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

	auto * inst = new BinaryInstruction(module->getCurrentFunction(), op, left->val, right->val, IntegerType::getTypeInt());
	node->blockInsts.addInst(left->blockInsts);
	node->blockInsts.addInst(right->blockInsts);
	node->blockInsts.addInst(inst);
	node->val = inst;

	return true;
}

// 整数加法AST节点翻译成线性中间IR
bool IRGenerator::ir_add(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_ADD_I);
}

// 整数减法AST节点翻译成线性中间IR
bool IRGenerator::ir_sub(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_SUB_I);
}

// 整数乘法AST节点翻译成线性中间IR
bool IRGenerator::ir_mul(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_MUL_I);
}

// 整数除法AST节点翻译成线性中间IR
bool IRGenerator::ir_div(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_DIV_I);
}

// 整数求余AST节点翻译成线性中间IR
bool IRGenerator::ir_mod(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_MOD_I);
}

// 整数相等比较AST节点翻译成线性中间IR
bool IRGenerator::ir_eq(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_CMP_EQ_I);
}

// 整数不等比较AST节点翻译成线性中间IR
bool IRGenerator::ir_ne(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_CMP_NE_I);
}

// 整数小于比较AST节点翻译成线性中间IR
bool IRGenerator::ir_lt(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_CMP_LT_I);
}

// 整数小于等于比较AST节点翻译成线性中间IR
bool IRGenerator::ir_le(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_CMP_LE_I);
}

// 整数大于比较AST节点翻译成线性中间IR
bool IRGenerator::ir_gt(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_CMP_GT_I);
}

// 整数大于等于比较AST节点翻译成线性中间IR
bool IRGenerator::ir_ge(ast_node * node)
{
	return ir_binary(node, IRInstOperator::IRINST_OP_CMP_GE_I);
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
	node->blockInsts.addInst(expr->blockInsts);
	node->blockInsts.addInst(new BinaryInstruction(
		module->getCurrentFunction(),
		IRInstOperator::IRINST_OP_CMP_EQ_I,
		expr->val,
		module->newConstInt(0),
		IntegerType::getTypeInt()));
	node->val = node->blockInsts.getInsts().back();

	return true;
}

// 逻辑与AST节点翻译成线性中间IR，按短路语义生成控制流
bool IRGenerator::ir_logical_and(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	Value * result = module->newVarValue(IntegerType::getTypeInt(), "", node != nullptr ? node->line_no : -1);
	if (result == nullptr) {
		report_ir_error("E1215", node != nullptr ? node->line_no : -1, "逻辑运算", "逻辑与结果临时变量创建失败");
		return false;
	}

	// 默认结果为0，只有左右两侧都为真时才改写成1
	ast_node * left = ir_visit_ast_node(node->sons[0]);
	if (!left) {
		return false;
	}

	auto * rhsLabel = new LabelInstruction(currentFunc);
	auto * trueLabel = new LabelInstruction(currentFunc);
	auto * endLabel = new LabelInstruction(currentFunc);

	node->blockInsts.addInst(new MoveInstruction(currentFunc, result, module->newConstInt(0)));
	node->blockInsts.addInst(left->blockInsts);
	// 左侧为假直接结束，为真才继续计算右侧
	node->blockInsts.addInst(new CondBranchInstruction(currentFunc, left->val, rhsLabel, endLabel));
	node->blockInsts.addInst(rhsLabel);

	ast_node * right = ir_visit_ast_node(node->sons[1]);
	if (!right) {
		return false;
	}

	node->blockInsts.addInst(right->blockInsts);
	node->blockInsts.addInst(new CondBranchInstruction(currentFunc, right->val, trueLabel, endLabel));
	node->blockInsts.addInst(trueLabel);
	node->blockInsts.addInst(new MoveInstruction(currentFunc, result, module->newConstInt(1)));
	node->blockInsts.addInst(new GotoInstruction(currentFunc, endLabel));
	node->blockInsts.addInst(endLabel);
	node->val = result;

	return true;
}

// 逻辑或AST节点翻译成线性中间IR，按短路语义生成控制流
bool IRGenerator::ir_logical_or(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	Value * result = module->newVarValue(IntegerType::getTypeInt(), "", node != nullptr ? node->line_no : -1);
	if (result == nullptr) {
		report_ir_error("E1216", node != nullptr ? node->line_no : -1, "逻辑运算", "逻辑或结果临时变量创建失败");
		return false;
	}

	// 默认结果为1，只有左右两侧都为假时才改写成0
	ast_node * left = ir_visit_ast_node(node->sons[0]);
	if (!left) {
		return false;
	}

	auto * rhsLabel = new LabelInstruction(currentFunc);
	auto * falseLabel = new LabelInstruction(currentFunc);
	auto * endLabel = new LabelInstruction(currentFunc);

	node->blockInsts.addInst(new MoveInstruction(currentFunc, result, module->newConstInt(1)));
	node->blockInsts.addInst(left->blockInsts);
	// 左侧为真直接结束，为假才继续计算右侧
	node->blockInsts.addInst(new CondBranchInstruction(currentFunc, left->val, endLabel, rhsLabel));
	node->blockInsts.addInst(rhsLabel);

	ast_node * right = ir_visit_ast_node(node->sons[1]);
	if (!right) {
		return false;
	}

	node->blockInsts.addInst(right->blockInsts);
	node->blockInsts.addInst(new CondBranchInstruction(currentFunc, right->val, endLabel, falseLabel));
	node->blockInsts.addInst(falseLabel);
	node->blockInsts.addInst(new MoveInstruction(currentFunc, result, module->newConstInt(0)));
	node->blockInsts.addInst(new GotoInstruction(currentFunc, endLabel));
	node->blockInsts.addInst(endLabel);
	node->val = result;

	return true;
}

// 判断一段IR是否已经以跳转/退出等终结指令结束
bool IRGenerator::block_ends_with_terminator(const InterCode & code) const
{
	const auto & insts = const_cast<InterCode &>(code).getInsts();
	if (insts.empty()) {
		return false;
	}

	IRInstOperator op = insts.back()->getOp();
	return op == IRInstOperator::IRINST_OP_GOTO || op == IRInstOperator::IRINST_OP_COND_BR ||
		   op == IRInstOperator::IRINST_OP_EXIT;
}

// if语句AST节点翻译成线性中间IR
bool IRGenerator::ir_if(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	ast_node * condNode = ir_visit_ast_node(node->sons[0]);
	if (!condNode) {
		return false;
	}

	auto * thenLabel = new LabelInstruction(currentFunc);
	auto * endLabel = new LabelInstruction(currentFunc);
	LabelInstruction * elseLabel = endLabel;
	if (node->sons.size() > 2 && node->sons[2] != nullptr) {
		elseLabel = new LabelInstruction(currentFunc);
	}

	// 先根据条件值跳then或else/end，再分别拼接分支代码
	node->blockInsts.addInst(condNode->blockInsts);
	node->blockInsts.addInst(new CondBranchInstruction(currentFunc, condNode->val, thenLabel, elseLabel));
	node->blockInsts.addInst(thenLabel);

	ast_node * thenNode = ir_visit_ast_node(node->sons[1]);
	if (!thenNode) {
		return false;
	}

	bool thenTerminated = block_ends_with_terminator(thenNode->blockInsts);
	node->blockInsts.addInst(thenNode->blockInsts);
	if (!thenTerminated) {
		node->blockInsts.addInst(new GotoInstruction(currentFunc, endLabel));
	}

	if (elseLabel != endLabel) {
		node->blockInsts.addInst(elseLabel);
		ast_node * elseNode = ir_visit_ast_node(node->sons[2]);
		if (!elseNode) {
			return false;
		}

		bool elseTerminated = block_ends_with_terminator(elseNode->blockInsts);
		node->blockInsts.addInst(elseNode->blockInsts);
		if (!elseTerminated) {
			node->blockInsts.addInst(new GotoInstruction(currentFunc, endLabel));
		}
	}

	node->blockInsts.addInst(endLabel);
	return true;
}

// while语句AST节点翻译成线性中间IR
bool IRGenerator::ir_while(ast_node * node)
{
	Function * currentFunc = module->getCurrentFunction();
	auto * condLabel = new LabelInstruction(currentFunc);
	auto * bodyLabel = new LabelInstruction(currentFunc);
	auto * endLabel = new LabelInstruction(currentFunc);

	// 记录当前循环的continue/break目标，供嵌套循环中的break/continue查栈顶
	loopTargets.emplace_back(condLabel, endLabel);

	node->blockInsts.addInst(condLabel);

	ast_node * condNode = ir_visit_ast_node(node->sons[0]);
	if (!condNode) {
		loopTargets.pop_back();
		return false;
	}

	node->blockInsts.addInst(condNode->blockInsts);
	node->blockInsts.addInst(new CondBranchInstruction(currentFunc, condNode->val, bodyLabel, endLabel));
	node->blockInsts.addInst(bodyLabel);

	ast_node * bodyNode = ir_visit_ast_node(node->sons[1]);
	if (!bodyNode) {
		loopTargets.pop_back();
		return false;
	}

	bool bodyTerminated = block_ends_with_terminator(bodyNode->blockInsts);
	node->blockInsts.addInst(bodyNode->blockInsts);
	if (!bodyTerminated) {
		node->blockInsts.addInst(new GotoInstruction(currentFunc, condLabel));
	}

	node->blockInsts.addInst(endLabel);
	loopTargets.pop_back();

	return true;
}

// break语句AST节点翻译成线性中间IR
bool IRGenerator::ir_break(ast_node * node)
{
	if (loopTargets.empty()) {
		report_ir_error("E1210", node != nullptr ? node->line_no : -1, "控制流检查", "break不在循环内");
		return false;
	}

	node->blockInsts.addInst(
		new GotoInstruction(module->getCurrentFunction(), loopTargets.back().second));
	return true;
}

// continue语句AST节点翻译成线性中间IR
bool IRGenerator::ir_continue(ast_node * node)
{
	if (loopTargets.empty()) {
		report_ir_error("E1211", node != nullptr ? node->line_no : -1, "控制流检查", "continue不在循环内");
		return false;
	}

	node->blockInsts.addInst(
		new GotoInstruction(module->getCurrentFunction(), loopTargets.back().first));
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

	// 赋值运算符的右侧操作数
	ast_node * right = ir_visit_ast_node(son2_node);
	if (!right) {
		return false;
	}

	// 检查左侧是否为常量
	if (left->val && left->val->isConst()) {
		report_ir_error(
			"E1303",
			son1_node->line_no,
			"常量赋值",
			"常量(%s)不能被重新赋值",
			son1_node->name.c_str());
		return false;
	}

	// 这里只处理整型的数据，如需支持实数，则需要针对类型进行处理

	MoveInstruction * movInst = new MoveInstruction(module->getCurrentFunction(), left->val, right->val);

	// 创建临时变量保存IR的值，以及线性IR指令
	node->blockInsts.addInst(right->blockInsts);
	node->blockInsts.addInst(left->blockInsts);
	node->blockInsts.addInst(movInst);

	// 这里假定赋值的类型是一致的
	node->val = movInst;

	return true;
}

// 变量声明语句节点翻译成线性中间IR
bool IRGenerator::ir_declare_statment(ast_node * node)
{
	bool result = false;

	for (auto & child: node->sons) {

		// 遍历声明项：普通变量与常量分别走各自语义路径
		if (child != nullptr && child->node_type == ast_operator_type::AST_OP_CONST_DECL) {
			result = ir_const_declaration(child);
		} else {
			result = ir_variable_declare(child);
		}
		if (!result) {
			break;
		}

		node->blockInsts.addInst(child->blockInsts);
	}

	return result;
}

// 变量定声明节点翻译成线性中间IR
bool IRGenerator::ir_variable_declare(ast_node * node)
{
	// 共有两个孩子，第一个类型，第二个变量名

	// TODO 这里可强化类型等检查

	node->val = module->newVarValue(node->sons[0]->type, node->sons[1]->name, node->sons[1]->line_no);
	if (node->val == nullptr) {
		return false;
	}

	if (node->sons.size() < 3) {
		return true;
	}

	ast_node * init_expr = node->sons[2];

	if (module->getCurrentFunction() == nullptr) {
		int32_t init_value = 0;
		if (!eval_global_const_expr(init_expr, init_value)) {
			report_ir_error(
				"E1301",
				node->sons[1] != nullptr ? node->sons[1]->line_no : -1,
				"全局初始化",
				"全局变量(%s)初始化目前只支持常量整数表达式",
				node->sons[1]->name.c_str());
			return false;
		}

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

		global_var->setInitializer(init_value);
		return true;
	}

	ast_node * init_node = ir_visit_ast_node(init_expr);
	if (!init_node) {
		return false;
	}

	node->blockInsts.addInst(init_node->blockInsts);
	node->blockInsts.addInst(new MoveInstruction(module->getCurrentFunction(), node->val, init_node->val));

	return true;
}

// 常量声明节点翻译成线性中间IR
bool IRGenerator::ir_const_declaration(ast_node * node)
{
	// 常量声明与变量声明结构类似，但 isConst 标志为 true
	// 第一个孩子是类型，第二个孩子是变量名，第三个孩子是初始化表达式

	if (node->sons.size() < 3) {
		report_ir_error(
			"E1304",
			node->sons[1] != nullptr ? node->sons[1]->line_no : -1,
			"常量定义",
			"常量(%s)定义时必须初始化",
			node->sons[1] != nullptr ? node->sons[1]->name.c_str() : "unknown");
		return false;
	}

	node->val = module->newVarValue(node->sons[0]->type, node->sons[1]->name, node->sons[1]->line_no);
	if (node->val == nullptr) {
		return false;
	}

	// 标记为常量
	node->val->setConst(true);

	if (module->getCurrentFunction() == nullptr) {
		// 全局常量
		int32_t init_value = 0;
		if (!eval_global_const_expr(node->sons[2], init_value)) {
			report_ir_error(
				"E1301",
				node->sons[1] != nullptr ? node->sons[1]->line_no : -1,
				"全局初始化",
				"全局常量(%s)初始化目前只支持常量整数表达式",
				node->sons[1]->name.c_str());
			return false;
		}

		auto * global_var = dynamic_cast<GlobalVariable *>(node->val);
		if (global_var == nullptr) {
			report_ir_error(
				"E1302",
				node->sons[1] != nullptr ? node->sons[1]->line_no : -1,
				"全局初始化",
				"全局常量(%s)初始化失败",
				node->sons[1]->name.c_str());
			return false;
		}

		global_var->setInitializer(init_value);
		return true;
	}

	// 局部常量
	ast_node * init_node = ir_visit_ast_node(node->sons[2]);
	if (!init_node) {
		return false;
	}

	node->blockInsts.addInst(init_node->blockInsts);
	node->blockInsts.addInst(new MoveInstruction(module->getCurrentFunction(), node->val, init_node->val));

	return true;
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

	// 当前函数存在函数调用
	currentFunc->setExistFuncCall(true);

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

			Type * realType = temp->val != nullptr ? temp->val->getType() : nullptr;
			Type * formalType = formalParams[index]->getType();
			if (realType == nullptr || formalType == nullptr || realType->getTypeID() != formalType->getTypeID()) {
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

			realParams.push_back(temp->val);
			node->blockInsts.addInst(temp->blockInsts);
		}
	}

	// 返回调用有返回值，则需要分配临时变量，用于保存函数调用的返回值
	Type * type = calledFunction->getReturnType();

	FuncCallInstruction * funcCallInst = new FuncCallInstruction(currentFunc, calledFunction, realParams, type);

	// 创建函数调用指令
	node->blockInsts.addInst(funcCallInst);

	// 函数调用结果Value保存到node中，可能为空，上层节点可利用这个值
	node->val = funcCallInst;

	return true;
}
