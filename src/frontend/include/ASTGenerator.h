#pragma once

#include <string>

#include "AST.h"

// AST生成器的接口类
class ASTGenerator {
public:
	// 构造函数
	explicit ASTGenerator(std::string _filename);

	// 析构函数
	virtual ~ASTGenerator()
	{}

	// 运行函数
	virtual bool run();

	// 返回抽象语法树的根
	[[nodiscard]] ast_node * getASTRoot() const
	{
		return astRoot;
	}

protected:
	// 要解析的文件路径
	std::string filename;

	// 抽象语法树的根
	ast_node * astRoot = nullptr;
};
