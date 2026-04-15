#pragma once

#include <unordered_map>

#include "frontend/include/AST.h"
#include "Module.h"


class IRGenerator {
public:
	// 构造函数
    IRGenerator(ast_node *root, Module *module);
	// 运行生成 module 产物
    bool run();

protected:
	// 编译单元AST节点翻译成线性中间IR
	bool ir_compile_unit(ast_node * node);

	// 函数定义AST节点翻译成线性中间IR
	bool ir_function_define(ast_node * node);

	// 形式参数AST节点翻译成线性中间IR
	bool ir_function_formal_params(ast_node * node);

	// 函数调用AST节点翻译成线性中间IR
	bool ir_function_call(ast_node * node);

	// 语句块（含函数体）AST节点翻译成线性中间IR
	bool ir_block(ast_node * node);

	// 整数加法AST节点翻译成线性中间IR
	bool ir_add(ast_node * node);

	// 整数减法AST节点翻译成线性中间IR
	bool ir_sub(ast_node * node);

	// 整数乘法AST节点翻译成线性中间IR
	bool ir_mul(ast_node * node);

	// 整数除法AST节点翻译成线性中间IR
	bool ir_div(ast_node * node);

	// 整数求余AST节点翻译成线性中间IR
	bool ir_mod(ast_node * node);

	// 赋值AST节点翻译成线性中间IR
	bool ir_assign(ast_node * node);

	// return节点翻译成线性中间IR
	bool ir_return(ast_node * node);

	// 类型叶子节点翻译成线性中间IR
	bool ir_leaf_node_type(ast_node * node);

	// 标识符叶子节点翻译成线性中间IR
	bool ir_leaf_node_var_id(ast_node * node);

	// 无符号整数字面量叶子节点翻译成线性中间IR
	bool ir_leaf_node_uint(ast_node * node);

	// float数字面量叶子节点翻译成线性中间IR
	bool ir_leaf_node_float(ast_node * node);

	// 变量声明语句节点翻译成线性中间IR
	bool ir_declare_statment(ast_node * node);

	// 变量定声明节点翻译成线性中间IR
	bool ir_variable_declare(ast_node * node);

	// 计算全局变量初始化的常量整数表达式
	bool eval_global_const_expr(ast_node * node, int32_t & value);

	// 未知节点类型的节点处理
	bool ir_default(ast_node * node);

	// 根据AST的节点运算符查找对应的翻译函数并执行翻译动作
	ast_node * ir_visit_ast_node(ast_node * node);

	/// @brief AST的节点操作函数
	typedef bool (IRGenerator::*ast2ir_handler_t)(ast_node *);

	/// @brief AST节点运算符与动作函数关联的映射表
	std::unordered_map<ast_operator_type, ast2ir_handler_t> ast2ir_handlers;


private:
	// 抽象语法树的根
	ast_node * root;

	// 符号表:模块
	Module * module;
};
