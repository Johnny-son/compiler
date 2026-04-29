// 抽象语法树AST管理的头文件

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "AttrType.h"
#include "ir/include/Value.h"
#include "ir/Types/VoidType.h"

///
// AST节点的类型。C++专门因为枚举类来区分C语言的结构体
///
enum class ast_operator_type : int {

	/* 以下为AST的叶子节点 */

	// 无符号整数字面量叶子节点
	AST_OP_LEAF_LITERAL_UINT,

	// 浮点数字面量叶子节点
	AST_OP_LEAF_LITERAL_FLOAT,

	// 变量ID叶子节点
	AST_OP_LEAF_VAR_ID,

	// 复杂类型的节点
	AST_OP_LEAF_TYPE,

	/* 以下为AST的内部节点，含根节点 */

	// 文件编译单元运算符，可包含函数定义、语句块等孩子
	AST_OP_COMPILE_UNIT,

	// 函数定义运算符，函数名和返回值类型作为节点的属性，自左到右孩子：AST_OP_FUNC_FORMAL_PARAMS、AST_OP_BLOCK
	AST_OP_FUNC_DEF,

	// 形式参数列表运算符，可包含多个孩子：AST_OP_FUNC_FORMAL_PARAM
	AST_OP_FUNC_FORMAL_PARAMS,

	// 形参运算符，属性包含名字与类型，复杂类型时可能要包含孩子
	AST_OP_FUNC_FORMAL_PARAM,

	// 函数调用运算符，函数名作为节点属性，孩子包含AST_OP_FUNC_REAL_PARAMS
	AST_OP_FUNC_CALL,

	// 实际参数列表运算符，可包含多个表达式AST_OP_EXPR
	AST_OP_FUNC_REAL_PARAMS,

	// 多个语句组成的块运算符，也称为复合语句
	AST_OP_BLOCK,

	// 符合语句，也就是语句块，两个名字一个运算符
	AST_OP_COMPOUNDSTMT = AST_OP_BLOCK,

	// return语句运算符
	AST_OP_RETURN,

	// 赋值语句运算符
	AST_OP_ASSIGN,

	// if语句运算符，孩子为条件、then分支、可选else分支
	AST_OP_IF,

	// while语句运算符，孩子为条件、循环体
	AST_OP_WHILE,

	// break语句运算符
	AST_OP_BREAK,

	// continue语句运算符
	AST_OP_CONTINUE,

	// 变量声明语句
	AST_OP_DECL_STMT,

	// 变量声明
	AST_OP_VAR_DECL,

	// 二元运算符+
	AST_OP_ADD,

	// 二元运算符-
	AST_OP_SUB,

	// 二元运算符*
	AST_OP_MUL,

	// 二元运算符/
	AST_OP_DIV,

	// 二元运算符%
	AST_OP_MOD,

	// 二元比较运算符==
	AST_OP_EQ,

	// 二元比较运算符!=
	AST_OP_NE,

	// 二元比较运算符<
	AST_OP_LT,

	// 二元比较运算符<=
	AST_OP_LE,

	// 二元比较运算符>
	AST_OP_GT,

	// 二元比较运算符>=
	AST_OP_GE,

	// 逻辑与运算符&&
	AST_OP_LAND,

	// 逻辑或运算符||
	AST_OP_LOR,

	// 逻辑非运算符!
	AST_OP_NOT,

	// 最大标识符，表示非法运算符
	AST_OP_MAX,
};

///
// 抽象语法树AST的节点描述类
///
class ast_node {

public:
	// 创建指定节点类型的节点
	// _node_type 节点类型
	ast_node(ast_operator_type _node_type, Type * _type = VoidType::getType(), int64_t _line_no = -1);

	// 构造函数
	// _type 节点值的类型
	ast_node(Type * _type);

	// 针对无符号整数字面量的构造函数
	// attr 无符号整数字面量
	ast_node(digit_int_attr attr);

	// 针对标识符ID的叶子构造函数
	// attr 字符型标识符
	ast_node(var_id_attr attr);

	// 针对标识符ID的叶子构造函数
	// _id 标识符ID
	// _line_no 行号
	ast_node(std::string id, int64_t _line_no);

	// 判断是否是叶子节点
	// type 节点类型
	// true：是叶子节点 false：内部节点
	bool isLeafNode();

	// 向父节点插入一个节点
	ast_node * insert_son_node(ast_node * node);

	// 创建无符号整数的叶子节点
	// val 词法值
	// line_no 行号
	static ast_node * New(digit_int_attr attr);

	// 创建标识符的叶子节点
	// val 词法值
	// line_no 行号
	static ast_node * New(var_id_attr attr);

	// 创建标识符的叶子节点
	// id 词法值
	// line_no 行号
	static ast_node * New(std::string id, int64_t lineno);

	// 创建具备指定类型的节点
	// type 节点值类型
	// line_no 行号
	static ast_node * New(Type * type);

	// 创建函数定义类型的内部AST节点
	// type_node 函数返回值类型
	// name_node 函数名节点
	// block 函数体语句块
	// params 函数形参，可以没有参数
	static ast_node * create_func_def(
		ast_node * type_node, ast_node * name_node, ast_node * block = nullptr, ast_node * params = nullptr);

	// 创建函数定义类型的内部AST节点
	// type 返回值类型
	// id 函数名字
	// block_node 函数体语句块节点
	// params_node 函数形参，可以没有参数
	static ast_node *
	create_func_def(type_attr & type, var_id_attr & id, ast_node * block_node, ast_node * params_node);

	// 创建函数形式参数的节点
	// line_no 行号
	// param_name 形式参数名
	static ast_node * create_func_formal_param(uint32_t line_no, const char * param_name);

	// 创建函数调用的节点
	// funcname_node 函数名节点
	// params_node 实参节点
	static ast_node * create_func_call(ast_node * funcname_node, ast_node * params_node = nullptr);

	// 创建类型节点
	// type 类型信息
	static ast_node * create_type_node(type_attr & type);

	// 类型属性转换成Type
	// attr 词法属性
	static Type * typeAttr2Type(type_attr & attr);

	// 根据第一个变量定义创建变量声明语句节点
	// first_child 第一个变量定义节点
	/// @return ast_node* 变量声明语句节点
	static ast_node * create_var_decl_stmt_node(ast_node * first_child);

	// 根据变量的类型和属性创建变量声明语句节点
	// type 变量的类型
	// id 变量的名字
	/// @return ast_node* 变量声明语句节点
	static ast_node * create_var_decl_stmt_node(type_attr & type, var_id_attr & id);

	// 根据类型创建变量声明节点
	// type 类型
	// id 变量属性
	/// @return ast_node* 类型声明节点
	static ast_node * createVarDeclNode(Type * type, var_id_attr & id);

	// 根据类型以及变量ID创建变量声明节点
	// type 类型属性
	// id 变量属性
	/// @return ast_node* 声明节点
	static ast_node * createVarDeclNode(type_attr & type, var_id_attr & id);

	// 向变量声明语句中追加变量声明
	// stmt_node 变量声明语句
	// id 变量的名字
	/// @return ast_node* 变量声明语句节点
	static ast_node * add_var_decl_node(ast_node * stmt_node, var_id_attr & id);

	// 释放节点
	static void Delete(ast_node * node);

	// 创建指定节点类型的节点（C++11 可变模板参数版本）
	// children 可变个数的孩子节点（仅接受 ast_node* 类型）
	template <typename... Args>
	static ast_node * New(ast_operator_type type, Args... children)
	{
		ast_node * parent_node = new ast_node(type);
		insertChildren(parent_node, children...);
		return parent_node;
	}

private:
	// 递归插入孩子节点的辅助函数（基础情况）
	static void insertChildren(ast_node * parent)
	{}

	// 递归插入孩子节点的辅助函数（仅接受 ast_node* 类型）
	template <typename... Args>
	static void insertChildren(ast_node * parent, ast_node * child, Args... rest)
	{
		if (child != nullptr) {
			parent->insert_son_node(child);
		}
		insertChildren(parent, rest...);
	}

public:
	// 节点类型
	ast_operator_type node_type;

	// 行号信息，主要针对叶子节点有用
	int64_t line_no;

	// 节点值的类型，可用于函数返回值类型
	Type * type;

	// 无符号整数字面量值
	uint32_t integer_val;

	// float类型字面量值
	float float_val;

	// 变量名，或者函数名
	std::string name;

	// 父节点
	ast_node * parent = nullptr;

	// 孩子节点
	std::vector<ast_node *> sons;

	// MiniLLVM IR生成过程中绑定到该AST节点的Value
	Value * val = nullptr;

	///
	// 在进入block等节点时是否要进行作用域管理。默认要做。
	///
	bool needScope = true;
};
