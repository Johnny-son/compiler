// Antlr4的具体语法树的遍历产生AST

#pragma once

#include "AST.h"
#include "MiniCBaseVisitor.h"

/// @brief 遍历具体语法树产生抽象语法树
class MiniCCSTVisitor : public MiniCBaseVisitor {

public:
	//构造函数
	MiniCCSTVisitor();

	// 析构函数
	virtual ~MiniCCSTVisitor();

	// 遍历CST产生AST
	ast_node * run(MiniCParser::CompUnitContext * root);

protected:
	/* 下面的函数都是从MiniCBaseVisitor继承下来的虚拟函数，需要重载实现 */

	// 非终结运算符compUnit的遍历
	std::any visitCompUnit(MiniCParser::CompUnitContext * ctx) override;

	// 非终结运算符funcDef的遍历
	std::any visitFuncDef(MiniCParser::FuncDefContext * ctx) override;

	// 非终结符FuncFParams的分析
	std::any visitFuncFParams(MiniCParser::FuncFParamsContext * ctx) override;

	// 非终结符FuncFParam的分析
	std::any visitFuncFParam(MiniCParser::FuncFParamContext * ctx) override;

	// 非终结符FuncArrayDims的分析
	std::any visitFuncArrayDims(MiniCParser::FuncArrayDimsContext * ctx) override;

	// 非终结运算符block的遍历
	std::any visitBlock(MiniCParser::BlockContext * ctx) override;

	// 非终结运算符blockItemList的遍历
	std::any visitBlockItemList(MiniCParser::BlockItemListContext * ctx) override;

	// 非终结运算符blockItem的遍历
	std::any visitBlockItem(MiniCParser::BlockItemContext * ctx) override;

	// 非终结运算符statement中的遍历
	std::any visitStatement(MiniCParser::StatementContext * ctx);

	// 非终结运算符statement中的returnStatement的遍历
	std::any visitReturnStatement(MiniCParser::ReturnStatementContext * ctx) override;

	// 内部产生的非终结符ifStatement的分析
	std::any visitIfStatement(MiniCParser::IfStatementContext * ctx) override;

	// 内部产生的非终结符whileStatement的分析
	std::any visitWhileStatement(MiniCParser::WhileStatementContext * ctx) override;

	// 内部产生的非终结符breakStatement的分析
	std::any visitBreakStatement(MiniCParser::BreakStatementContext * ctx) override;

	// 内部产生的非终结符continueStatement的分析
	std::any visitContinueStatement(MiniCParser::ContinueStatementContext * ctx) override;

	// 非终结运算符expr的遍历
	std::any visitExpr(MiniCParser::ExprContext * ctx) override;

	// 非终结符LOrExp的分析
	std::any visitLOrExp(MiniCParser::LOrExpContext * ctx) override;

	// 非终结符LAndExp的分析
	std::any visitLAndExp(MiniCParser::LAndExpContext * ctx) override;

	// 非终结符EqExp的分析
	std::any visitEqExp(MiniCParser::EqExpContext * ctx) override;

	// 非终结符RelExp的分析
	std::any visitRelExp(MiniCParser::RelExpContext * ctx) override;

	// 内部产生的非终结符assignStatement的分析
	std::any visitAssignStatement(MiniCParser::AssignStatementContext * ctx) override;

	// 内部产生的非终结符blockStatement的分析
	std::any visitBlockStatement(MiniCParser::BlockStatementContext * ctx) override;

	// 非终结符AddExp的分析
	std::any visitAddExp(MiniCParser::AddExpContext * ctx) override;

	// 非终结符MulExp的分析
	std::any visitMulExp(MiniCParser::MulExpContext * ctx) override;

	// 非终结符addOp的分析
	std::any visitAddOp(MiniCParser::AddOpContext * ctx) override;

	// 非终结符mulOp的分析
	std::any visitMulOp(MiniCParser::MulOpContext * ctx) override;

	// 非终结符unaryExp的分析
	std::any visitUnaryExp(MiniCParser::UnaryExpContext * ctx) override;

	// 非终结符unaryOp的分析
	std::any visitUnaryOp(MiniCParser::UnaryOpContext * ctx) override;

	// 非终结符PrimaryExp的分析
	std::any visitPrimaryExp(MiniCParser::PrimaryExpContext * ctx) override;

	// 非终结符LVal的分析
	std::any visitLVal(MiniCParser::LValContext * ctx) override;

	// 非终结符ArrayDims的分析
	std::any visitArrayDims(MiniCParser::ArrayDimsContext * ctx) override;

	// 非终结符InitVal的分析
	std::any visitInitVal(MiniCParser::InitValContext * ctx) override;

	// 非终结符VarDecl的分析
	std::any visitVarDecl(MiniCParser::VarDeclContext * ctx) override;

	// 非终结符ConstDecl的分析
	std::any visitConstDecl(MiniCParser::ConstDeclContext * ctx) override;

	// 非终结符VarDecl的分析
	std::any visitVarDef(MiniCParser::VarDefContext * ctx) override;

	// 非终结符BasicType的分析
	std::any visitBasicType(MiniCParser::BasicTypeContext * ctx) override;

	// 非终结符RealParamList的分析
	std::any visitRealParamList(MiniCParser::RealParamListContext * ctx) override;

	// 非终结符ExpressionStatement的分析
	std::any visitExpressionStatement(MiniCParser::ExpressionStatementContext * context) override;
};
