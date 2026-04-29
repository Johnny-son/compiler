// Antlr4的具体语法树的遍历产生AST

#include <string>
#include <vector>

#include "CSTVisitor.h"
#include "AST.h"
#include "AttrType.h"
#include "ir/Types/IntegerType.h"

#define Instanceof(res, type, var) auto res = dynamic_cast<type>(var)

namespace {

// 按照左结合规则把操作数和操作符折叠成二叉表达式树
// 用于关系、相等、逻辑表达式，避免在每一层文法里重复手写相同构树逻辑
ast_node * buildLeftAssociativeBinaryTree(
	const std::vector<ast_node *> & operands, const std::vector<ast_operator_type> & operators, int64_t lineNo)
{
	if (operands.empty()) {
		return nullptr;
	}

	ast_node * left = operands.front();
	for (size_t index = 0; index < operators.size(); ++index) {
		auto * node = ast_node::New(operators[index], left, operands[index + 1]);
		node->line_no = lineNo;
		node->type = IntegerType::getTypeInt();
		left = node;
	}

	return left;
}

} // namespace

// 构造函数
MiniCCSTVisitor::MiniCCSTVisitor()
{}

// 析构函数
MiniCCSTVisitor::~MiniCCSTVisitor()
{}

// 遍历CST产生AST
// 输入 CST 语法树的根结点 返回 AST 的根节点
ast_node * MiniCCSTVisitor::run(MiniCParser::CompUnitContext * root)
{
	return std::any_cast<ast_node *>(visitCompUnit(root));
}

// 非终结运算符compUnit的遍历
std::any MiniCCSTVisitor::visitCompUnit(MiniCParser::CompUnitContext * ctx)
{
	// compUnit: (funcDef | varDecl | constDecl)* EOF
	ast_node * compUnitNode = ast_node::New(ast_operator_type::AST_OP_COMPILE_UNIT);

	// 按照源码出现顺序收集顶层声明和函数定义，避免前端阶段重排
	for (auto * child: ctx->children) {
		if (auto * varCtx = dynamic_cast<MiniCParser::VarDeclContext *>(child); varCtx != nullptr) {
			auto * tempNode = std::any_cast<ast_node *>(visitVarDecl(varCtx));
			(void) compUnitNode->insert_son_node(tempNode);
		} else if (auto * funcCtx = dynamic_cast<MiniCParser::FuncDefContext *>(child); funcCtx != nullptr) {
			auto * tempNode = std::any_cast<ast_node *>(visitFuncDef(funcCtx));
			(void) compUnitNode->insert_son_node(tempNode);
		} else if (auto * constCtx = dynamic_cast<MiniCParser::ConstDeclContext *>(child); constCtx != nullptr) {
			auto * tempNode = std::any_cast<ast_node *>(visitConstDecl(constCtx));
			(void) compUnitNode->insert_son_node(tempNode);
		}
	}

	return compUnitNode;
}

// 非终结运算符funcDef的遍历
std::any MiniCCSTVisitor::visitFuncDef(MiniCParser::FuncDefContext * ctx)
{
	// 识别的文法产生式：funcDef : T_INT T_ID T_L_PAREN funcFParams? T_R_PAREN block;

	// 函数返回类型，终结符
	type_attr funcReturnType{BasicType::TYPE_INT, (int64_t) ctx->T_INT()->getSymbol()->getLine()};

	// 创建函数名的标识符终结符节点，终结符
	char * id = strdup(ctx->T_ID()->getText().c_str());

	var_id_attr funcId{id, (int64_t) ctx->T_ID()->getSymbol()->getLine()};

	ast_node * formalParamsNode = nullptr;
	if (ctx->funcFParams()) {
		formalParamsNode = std::any_cast<ast_node *>(visitFuncFParams(ctx->funcFParams()));
	}

	// 遍历block结点创建函数体节点，非终结符
	auto blockNode = std::any_cast<ast_node *>(visitBlock(ctx->block()));

	// 创建函数定义的节点，孩子有类型，函数名，语句块和形参(实际上无)
	// create_func_def函数内会释放funcId中指向的标识符空间，切记，之后不要再释放，之前一定要是通过strdup函数或者malloc分配的空间
	return ast_node::create_func_def(funcReturnType, funcId, blockNode, formalParamsNode);
}

std::any MiniCCSTVisitor::visitFuncFParams(MiniCParser::FuncFParamsContext * ctx)
{
	// 识别的文法产生式：funcFParams: funcFParam (T_COMMA funcFParam)*;
	auto paramsNode = ast_node::New(ast_operator_type::AST_OP_FUNC_FORMAL_PARAMS);

	// 形参列表中的每个孩子都对应一个AST_OP_FUNC_FORMAL_PARAM
	for (auto paramCtx: ctx->funcFParam()) {
		auto paramNode = std::any_cast<ast_node *>(visitFuncFParam(paramCtx));
		paramsNode->insert_son_node(paramNode);
	}

	return paramsNode;
}

std::any MiniCCSTVisitor::visitFuncFParam(MiniCParser::FuncFParamContext * ctx)
{
	// 识别的文法产生式：funcFParam: T_INT T_ID;
	// 当前仅支持int标量形参，因此直接记录名字和行号即可
	int64_t lineNo = (int64_t) ctx->T_ID()->getSymbol()->getLine();
	return ast_node::create_func_formal_param((uint32_t) lineNo, ctx->T_ID()->getText().c_str());
}

// 非终结运算符block的遍历
std::any MiniCCSTVisitor::visitBlock(MiniCParser::BlockContext * ctx)
{
	// 识别的文法产生式：block : T_L_BRACE blockItemList? T_R_BRACE';
	if (!ctx->blockItemList()) {
		// 语句块没有语句

		// 为了方便创建一个空的Block节点
		return ast_node::New(ast_operator_type::AST_OP_BLOCK);
	}

	// 语句块含有语句

	// 内部创建Block节点，并把语句加入，这里不需要创建Block节点
	return visitBlockItemList(ctx->blockItemList());
}

// 非终结运算符blockItemList的遍历
std::any MiniCCSTVisitor::visitBlockItemList(MiniCParser::BlockItemListContext * ctx)
{
	// 识别的文法产生式：blockItemList : blockItem +;
	// 正闭包 循环 至少一个blockItem
	auto block_node = ast_node::New(ast_operator_type::AST_OP_BLOCK);

	for (auto blockItemCtx: ctx->blockItem()) {

		// 非终结符，需遍历
		auto blockItem = std::any_cast<ast_node *>(visitBlockItem(blockItemCtx));

		// 插入到块节点中
		(void) block_node->insert_son_node(blockItem);
	}

	return block_node;
}

// 非终结运算符blockItem的遍历
std::any MiniCCSTVisitor::visitBlockItem(MiniCParser::BlockItemContext * ctx)
{
	// 识别的文法产生式：blockItem : statement | varDecl | constDecl
	if (ctx->statement()) {
		// 语句识别

		return visitStatement(ctx->statement());
	} else if (ctx->varDecl()) {
		return visitVarDecl(ctx->varDecl());
	} else if (ctx->constDecl()) {
		return visitConstDecl(ctx->constDecl());
	}

	return (ast_node *) nullptr;
}

// 非终结运算符statement中的遍历
std::any MiniCCSTVisitor::visitStatement(MiniCParser::StatementContext * ctx)
{
	// statement:
	//   T_RETURN expr? T_SEMICOLON
	//   | lVal T_ASSIGN expr T_SEMICOLON
	//   | T_IF T_L_PAREN expr T_R_PAREN statement (T_ELSE statement)?
	//   | T_WHILE T_L_PAREN expr T_R_PAREN statement
	//   | T_BREAK T_SEMICOLON
	//   | T_CONTINUE T_SEMICOLON
	//   | block
	//   | expr? T_SEMICOLON;
	// statement在文法里采用了#label分支，这里按实际Context类型做分发
	if (Instanceof(assignCtx, MiniCParser::AssignStatementContext *, ctx)) {
		return visitAssignStatement(assignCtx);
	} else if (Instanceof(returnCtx, MiniCParser::ReturnStatementContext *, ctx)) {
		return visitReturnStatement(returnCtx);
	} else if (Instanceof(ifCtx, MiniCParser::IfStatementContext *, ctx)) {
		return visitIfStatement(ifCtx);
	} else if (Instanceof(whileCtx, MiniCParser::WhileStatementContext *, ctx)) {
		return visitWhileStatement(whileCtx);
	} else if (Instanceof(breakCtx, MiniCParser::BreakStatementContext *, ctx)) {
		return visitBreakStatement(breakCtx);
	} else if (Instanceof(continueCtx, MiniCParser::ContinueStatementContext *, ctx)) {
		return visitContinueStatement(continueCtx);
	} else if (Instanceof(blockCtx, MiniCParser::BlockStatementContext *, ctx)) {
		return visitBlockStatement(blockCtx);
	} else if (Instanceof(exprCtx, MiniCParser::ExpressionStatementContext *, ctx)) {
		return visitExpressionStatement(exprCtx);
	}

	return (ast_node *) nullptr;
}

// 非终结运算符statement中的returnStatement的遍历
std::any MiniCCSTVisitor::visitReturnStatement(MiniCParser::ReturnStatementContext * ctx)
{
	// 识别的文法产生式：returnStatement -> T_RETURN expr? T_SEMICOLON
	if (ctx->expr() == nullptr) {
		// return; 直接创建不带孩子的返回节点
		auto * returnNode = ast_node::New(ast_operator_type::AST_OP_RETURN);
		returnNode->line_no = (int64_t) ctx->T_RETURN()->getSymbol()->getLine();
		return returnNode;
	}

	auto * exprNode = std::any_cast<ast_node *>(visitExpr(ctx->expr()));
	auto * returnNode = ast_node::New(ast_operator_type::AST_OP_RETURN, exprNode);
	returnNode->line_no = (int64_t) ctx->T_RETURN()->getSymbol()->getLine();
	return returnNode;
}

// 非终结运算符expr的遍历
std::any MiniCCSTVisitor::visitExpr(MiniCParser::ExprContext * ctx)
{
	// 识别产生式：expr: lOrExp;
	return visitLOrExp(ctx->lOrExp());
}

// if语句AST构造，孩子顺序固定为条件、then分支、可选else分支
std::any MiniCCSTVisitor::visitIfStatement(MiniCParser::IfStatementContext * ctx)
{
	auto * condNode = std::any_cast<ast_node *>(visitExpr(ctx->expr()));
	auto * thenNode = std::any_cast<ast_node *>(visitStatement(ctx->statement(0)));
	if (thenNode == nullptr) {
		// if后面是空语句时，用一个不引入作用域的空block占位，方便中端统一处理
		thenNode = ast_node::New(ast_operator_type::AST_OP_BLOCK);
		thenNode->needScope = false;
	}

	ast_node * elseNode = nullptr;
	if (ctx->statement().size() > 1) {
		elseNode = std::any_cast<ast_node *>(visitStatement(ctx->statement(1)));
		if (elseNode == nullptr) {
			// else后面是空语句时，同样补一个空block占位
			elseNode = ast_node::New(ast_operator_type::AST_OP_BLOCK);
			elseNode->needScope = false;
		}
	}

	auto * ifNode = ast_node::New(ast_operator_type::AST_OP_IF, condNode, thenNode, elseNode);
	ifNode->line_no = (int64_t) ctx->T_IF()->getSymbol()->getLine();
	return ifNode;
}

// while语句AST构造，孩子顺序固定为条件和循环体
std::any MiniCCSTVisitor::visitWhileStatement(MiniCParser::WhileStatementContext * ctx)
{
	auto * condNode = std::any_cast<ast_node *>(visitExpr(ctx->expr()));
	auto * bodyNode = std::any_cast<ast_node *>(visitStatement(ctx->statement()));
	if (bodyNode == nullptr) {
		// while后面是空语句时，补一个不引入作用域的空block占位
		bodyNode = ast_node::New(ast_operator_type::AST_OP_BLOCK);
		bodyNode->needScope = false;
	}

	auto * whileNode = ast_node::New(ast_operator_type::AST_OP_WHILE, condNode, bodyNode);
	whileNode->line_no = (int64_t) ctx->T_WHILE()->getSymbol()->getLine();
	return whileNode;
}

// break语句在AST中是无孩子节点，仅保留行号供后续诊断使用
std::any MiniCCSTVisitor::visitBreakStatement(MiniCParser::BreakStatementContext * ctx)
{
	auto * node = ast_node::New(ast_operator_type::AST_OP_BREAK);
	node->line_no = (int64_t) ctx->T_BREAK()->getSymbol()->getLine();
	return node;
}

// continue语句在AST中是无孩子节点，仅保留行号供后续诊断使用
std::any MiniCCSTVisitor::visitContinueStatement(MiniCParser::ContinueStatementContext * ctx)
{
	auto * node = ast_node::New(ast_operator_type::AST_OP_CONTINUE);
	node->line_no = (int64_t) ctx->T_CONTINUE()->getSymbol()->getLine();
	return node;
}

// 逻辑或表达式按左结合构造，保持与文法层优先级一致
std::any MiniCCSTVisitor::visitLOrExp(MiniCParser::LOrExpContext * ctx)
{
	std::vector<ast_node *> operands;
	std::vector<ast_operator_type> operators;

	// 先收集全部逻辑与子表达式作为操作数
	for (auto * child: ctx->lAndExp()) {
		operands.push_back(std::any_cast<ast_node *>(visitLAndExp(child)));
	}

	// 逻辑或这一层的操作符只有一种，数量与T_LOR token个数一致
	operators.assign(ctx->T_LOR().size(), ast_operator_type::AST_OP_LOR);

	return buildLeftAssociativeBinaryTree(
		operands, operators, (int64_t) ctx->getStart()->getLine());
}

// 逻辑与表达式按左结合构造，保持与文法层优先级一致
std::any MiniCCSTVisitor::visitLAndExp(MiniCParser::LAndExpContext * ctx)
{
	std::vector<ast_node *> operands;
	std::vector<ast_operator_type> operators;

	// 先收集全部相等性子表达式作为操作数
	for (auto * child: ctx->eqExp()) {
		operands.push_back(std::any_cast<ast_node *>(visitEqExp(child)));
	}

	// 逻辑与这一层的操作符只有一种，数量与T_LAND token个数一致
	operators.assign(ctx->T_LAND().size(), ast_operator_type::AST_OP_LAND);

	return buildLeftAssociativeBinaryTree(
		operands, operators, (int64_t) ctx->getStart()->getLine());
}

// 相等性表达式需要从CST孩子里区分==和!=，再按左结合构树
std::any MiniCCSTVisitor::visitEqExp(MiniCParser::EqExpContext * ctx)
{
	std::vector<ast_node *> operands;
	std::vector<ast_operator_type> operators;

	// 先收集全部关系表达式作为操作数
	for (auto * child: ctx->relExp()) {
		operands.push_back(std::any_cast<ast_node *>(visitRelExp(child)));
	}

	// 再按出现顺序扫描终结符，恢复真正的比较操作符类型
	for (auto * child: ctx->children) {
		if (auto * terminal = dynamic_cast<antlr4::tree::TerminalNode *>(child); terminal != nullptr) {
			if (terminal->getSymbol()->getType() == MiniCParser::T_EQ) {
				operators.push_back(ast_operator_type::AST_OP_EQ);
			} else if (terminal->getSymbol()->getType() == MiniCParser::T_NE) {
				operators.push_back(ast_operator_type::AST_OP_NE);
			}
		}
	}

	return buildLeftAssociativeBinaryTree(
		operands, operators, (int64_t) ctx->getStart()->getLine());
}

// 关系表达式需要从CST孩子里区分<、<=、>、>=，再按左结合构树
std::any MiniCCSTVisitor::visitRelExp(MiniCParser::RelExpContext * ctx)
{
	std::vector<ast_node *> operands;
	std::vector<ast_operator_type> operators;

	// 关系表达式的底层操作数是加减表达式
	for (auto * child: ctx->addExp()) {
		operands.push_back(std::any_cast<ast_node *>(visitAddExp(child)));
	}

	// 顺序扫描终结符，恢复真正的关系运算符
	for (auto * child: ctx->children) {
		if (auto * terminal = dynamic_cast<antlr4::tree::TerminalNode *>(child); terminal != nullptr) {
			switch (terminal->getSymbol()->getType()) {
				case MiniCParser::T_LT:
					operators.push_back(ast_operator_type::AST_OP_LT);
					break;
				case MiniCParser::T_LE:
					operators.push_back(ast_operator_type::AST_OP_LE);
					break;
				case MiniCParser::T_GT:
					operators.push_back(ast_operator_type::AST_OP_GT);
					break;
				case MiniCParser::T_GE:
					operators.push_back(ast_operator_type::AST_OP_GE);
					break;
				default:
					break;
			}
		}
	}

	return buildLeftAssociativeBinaryTree(
		operands, operators, (int64_t) ctx->getStart()->getLine());
}

std::any MiniCCSTVisitor::visitAssignStatement(MiniCParser::AssignStatementContext * ctx)
{
	// 识别文法产生式：assignStatement: lVal T_ASSIGN expr T_SEMICOLON

	// 赋值左侧左值Lval遍历产生节点
	auto lvalNode = std::any_cast<ast_node *>(visitLVal(ctx->lVal()));

	// 赋值右侧expr遍历
	auto exprNode = std::any_cast<ast_node *>(visitExpr(ctx->expr()));

	// 创建一个AST_OP_ASSIGN类型的中间节点，孩子为Lval和Expr
	return ast_node::New(ast_operator_type::AST_OP_ASSIGN, lvalNode, exprNode);
}

std::any MiniCCSTVisitor::visitBlockStatement(MiniCParser::BlockStatementContext * ctx)
{
	// 识别文法产生式 blockStatement: block

	return visitBlock(ctx->block());
}

std::any MiniCCSTVisitor::visitAddExp(MiniCParser::AddExpContext * ctx)
{
	// 识别的文法产生式：addExp : mulExp (addOp mulExp)*;

	if (ctx->addOp().empty()) {

		// 没有addOp运算符，则说明闭包识别为0，只识别了第一个非终结符mulExp
		return visitMulExp(ctx->mulExp()[0]);
	}

	ast_node *left, *right;

	// 存在addOp运算符，自
	auto opsCtxVec = ctx->addOp();

	// 有操作符，肯定会进循环，使得right设置正确的值
	for (int k = 0; k < (int) opsCtxVec.size(); k++) {

		// 获取运算符
		ast_operator_type op = std::any_cast<ast_operator_type>(visitAddOp(opsCtxVec[k]));

		if (k == 0) {

			// 左操作数
			left = std::any_cast<ast_node *>(visitMulExp(ctx->mulExp()[k]));
		}

		// 右操作数
		right = std::any_cast<ast_node *>(visitMulExp(ctx->mulExp()[k + 1]));

		// 新建结点作为下一个运算符的右操作符
		left = ast_node::New(op, left, right);
	}

	return left;
}

std::any MiniCCSTVisitor::visitMulExp(MiniCParser::MulExpContext * ctx)
{
	// 识别的文法产生式：mulExp : unaryExp (mulOp unaryExp)*;

	if (ctx->mulOp().empty()) {
		return visitUnaryExp(ctx->unaryExp()[0]);
	}

	ast_node * left = nullptr;
	ast_node * right = nullptr;

	auto opsCtxVec = ctx->mulOp();

	for (int k = 0; k < (int) opsCtxVec.size(); k++) {

		ast_operator_type op = std::any_cast<ast_operator_type>(visitMulOp(opsCtxVec[k]));

		if (k == 0) {
			left = std::any_cast<ast_node *>(visitUnaryExp(ctx->unaryExp()[k]));
		}

		right = std::any_cast<ast_node *>(visitUnaryExp(ctx->unaryExp()[k + 1]));
		left = ast_node::New(op, left, right);
	}

	return left;
}

// 非终结运算符addOp的遍历
std::any MiniCCSTVisitor::visitAddOp(MiniCParser::AddOpContext * ctx)
{
	// 识别的文法产生式：addOp : T_ADD | T_SUB

	if (ctx->T_ADD()) {
		return ast_operator_type::AST_OP_ADD;
	} else {
		return ast_operator_type::AST_OP_SUB;
	}
}

std::any MiniCCSTVisitor::visitMulOp(MiniCParser::MulOpContext * ctx)
{
	// 识别的文法产生式：mulOp : T_MUL | T_DIV | T_MOD

	if (ctx->T_MUL()) {
		return ast_operator_type::AST_OP_MUL;
	} else if (ctx->T_DIV()) {
		return ast_operator_type::AST_OP_DIV;
	} else {
		return ast_operator_type::AST_OP_MOD;
	}
}

std::any MiniCCSTVisitor::visitUnaryExp(MiniCParser::UnaryExpContext * ctx)
{
	// 识别文法产生式：unaryExp: unaryOp unaryExp | primaryExp | T_ID T_L_PAREN realParamList? T_R_PAREN;

	if (ctx->unaryOp()) {
		// 一元表达式递归构造，优先把内层unaryExp先翻译成AST
		auto op = std::any_cast<ast_operator_type>(visitUnaryOp(ctx->unaryOp()));
		auto * exprNode = std::any_cast<ast_node *>(visitUnaryExp(ctx->unaryExp()));
		int64_t lineNo = (int64_t) ctx->getStart()->getLine();

		if (op == ast_operator_type::AST_OP_ADD) {
			// 一元正号对当前子集没有额外语义，直接透传操作数
			return exprNode;
		}

		if (op == ast_operator_type::AST_OP_SUB) {
			// 一元负号翻译成0-expr，复用后续中端已有的二元减法处理
			auto * zeroNode = ast_node::New(digit_int_attr{0, lineNo});
			auto * node = ast_node::New(ast_operator_type::AST_OP_SUB, zeroNode, exprNode);
			node->line_no = lineNo;
			node->type = IntegerType::getTypeInt();
			return node;
		}

		// 逻辑非保留为独立AST节点，后续在中端翻译成比较或短路逻辑
		auto * node = ast_node::New(ast_operator_type::AST_OP_NOT, exprNode);
		node->line_no = lineNo;
		node->type = IntegerType::getTypeInt();
		return node;
	}

	if (ctx->primaryExp()) {
		// 普通表达式
		return visitPrimaryExp(ctx->primaryExp());
	} else if (ctx->T_ID()) {

		// 创建函数调用名终结符节点
		ast_node * funcname_node = ast_node::New(ctx->T_ID()->getText(), (int64_t) ctx->T_ID()->getSymbol()->getLine());

		// 实参列表
		ast_node * paramListNode = nullptr;

		// 函数调用
		if (ctx->realParamList()) {
			// 有参数
			paramListNode = std::any_cast<ast_node *>(visitRealParamList(ctx->realParamList()));
		}

		// 创建函数调用节点，其孩子为被调用函数名和实参，
		return ast_node::create_func_call(funcname_node, paramListNode);
	} else {
		return (ast_node *) nullptr;
	}
}

std::any MiniCCSTVisitor::visitUnaryOp(MiniCParser::UnaryOpContext * ctx)
{
	// unaryOp只有+、-、!三种，直接映射到统一的AST运算符
	if (ctx->T_ADD()) {
		return ast_operator_type::AST_OP_ADD;
	}
	if (ctx->T_SUB()) {
		return ast_operator_type::AST_OP_SUB;
	}

	return ast_operator_type::AST_OP_NOT;
}

std::any MiniCCSTVisitor::visitPrimaryExp(MiniCParser::PrimaryExpContext * ctx)
{
	// 识别文法产生式 primaryExp: T_L_PAREN expr T_R_PAREN | T_DIGIT | lVal;

	ast_node * node = nullptr;

	if (ctx->T_DIGIT()) {
		// 无符号整型字面量
		// 识别 primaryExp: T_DIGIT

		uint32_t val = (uint32_t) stoull(ctx->T_DIGIT()->getText(), nullptr, 0);
		int64_t lineNo = (int64_t) ctx->T_DIGIT()->getSymbol()->getLine();
		node = ast_node::New(digit_int_attr{val, lineNo});
	} else if (ctx->lVal()) {
		// 具有左值的表达式
		// 识别 primaryExp: lVal
		node = std::any_cast<ast_node *>(visitLVal(ctx->lVal()));
	} else if (ctx->expr()) {
		// 带有括号的表达式
		// primaryExp: T_L_PAREN expr T_R_PAREN
		node = std::any_cast<ast_node *>(visitExpr(ctx->expr()));
	}

	return node;
}

std::any MiniCCSTVisitor::visitLVal(MiniCParser::LValContext * ctx)
{
	// 识别文法产生式：lVal: T_ID;
	// 获取ID的名字
	auto varId = ctx->T_ID()->getText();

	// 获取行号
	int64_t lineNo = (int64_t) ctx->T_ID()->getSymbol()->getLine();

	return ast_node::New(varId, lineNo);
}

std::any MiniCCSTVisitor::visitVarDecl(MiniCParser::VarDeclContext * ctx)
{
	// varDecl: basicType varDef (T_COMMA varDef)* T_SEMICOLON;

	// 声明语句节点
	ast_node * stmt_node = ast_node::New(ast_operator_type::AST_OP_DECL_STMT);

	// 类型节点
	type_attr typeAttr = std::any_cast<type_attr>(visitBasicType(ctx->basicType()));

	for (auto & varCtx: ctx->varDef()) {
		// 变量名节点
		ast_node * id_node = std::any_cast<ast_node *>(visitVarDef(varCtx));

		// 创建类型节点
		ast_node * type_node = ast_node::create_type_node(typeAttr);

		// 创建变量定义节点
		ast_node * decl_node = ast_node::New(ast_operator_type::AST_OP_VAR_DECL, type_node, id_node);
		decl_node->type = type_node->type;

		// 变量初始化表达式，作为第三个孩子追加到变量定义节点
		if (varCtx->expr()) {
			ast_node * init_node = std::any_cast<ast_node *>(visitExpr(varCtx->expr()));
			(void) decl_node->insert_son_node(init_node);
		}

		// 插入到变量声明语句
		(void) stmt_node->insert_son_node(decl_node);
	}

	return stmt_node;
}

std::any MiniCCSTVisitor::visitVarDef(MiniCParser::VarDefContext * ctx)
{
	// varDef: T_ID (T_ASSIGN expr)?;

	auto varId = ctx->T_ID()->getText();

	// 获取行号
	int64_t lineNo = (int64_t) ctx->T_ID()->getSymbol()->getLine();

	return ast_node::New(varId, lineNo);
}

std::any MiniCCSTVisitor::visitConstDecl(MiniCParser::ConstDeclContext * ctx)
{
	// constDecl: T_CONST basicType constDef (T_COMMA constDef)* T_SEMICOLON;

	// 声明语句节点
	ast_node * stmt_node = ast_node::New(ast_operator_type::AST_OP_DECL_STMT);

	// 类型节点
	type_attr typeAttr = std::any_cast<type_attr>(visitBasicType(ctx->basicType()));

	for (auto & constCtx: ctx->constDef()) {
		// 获取行号
		int64_t lineNo = (int64_t) constCtx->T_ID()->getSymbol()->getLine();

		// 创建标识符节点
		ast_node * id_node = ast_node::New(constCtx->T_ID()->getText(), lineNo);

		// 创建类型节点
		ast_node * type_node = ast_node::create_type_node(typeAttr);

		// 创建常量定义节点
		ast_node * decl_node = ast_node::New(ast_operator_type::AST_OP_CONST_DECL, type_node, id_node);
		decl_node->type = type_node->type;
		decl_node->isConst = true;

		// 初始化表达式
		ast_node * init_node = std::any_cast<ast_node *>(visitExpr(constCtx->expr()));
		(void) decl_node->insert_son_node(init_node);

		// 插入到常量声明语句
		(void) stmt_node->insert_son_node(decl_node);
	}

	return stmt_node;
}

std::any MiniCCSTVisitor::visitBasicType(MiniCParser::BasicTypeContext * ctx)
{
	// basicType: T_INT;
	type_attr attr{BasicType::TYPE_VOID, -1};
	if (ctx->T_INT()) {
		attr.type = BasicType::TYPE_INT;
		attr.lineno = (int64_t) ctx->T_INT()->getSymbol()->getLine();
	}

	return attr;
}

std::any MiniCCSTVisitor::visitRealParamList(MiniCParser::RealParamListContext * ctx)
{
	// 识别的文法产生式：realParamList : expr (T_COMMA expr)*;

	auto paramListNode = ast_node::New(ast_operator_type::AST_OP_FUNC_REAL_PARAMS);

	for (auto paramCtx: ctx->expr()) {

		auto paramNode = std::any_cast<ast_node *>(visitExpr(paramCtx));

		paramListNode->insert_son_node(paramNode);
	}

	return paramListNode;
}

std::any MiniCCSTVisitor::visitExpressionStatement(MiniCParser::ExpressionStatementContext * ctx)
{
	// 识别文法产生式  expr ? T_SEMICOLON #expressionStatement;
	if (ctx->expr()) {
		// 表达式语句

		// 遍历expr非终结符，创建表达式节点后返回
		return visitExpr(ctx->expr());
	} else {
		// 空语句

		// 直接返回空指针，需要再把语句加入到语句块时要注意判断，空语句不要加入
		return (ast_node *) nullptr;
	}
}
