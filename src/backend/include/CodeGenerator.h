// 代码生成器共同类的头文件

#pragma once

#include <cstdio>
#include <string>

#include "Module.h"

// 代码生成的一般类
class CodeGenerator {

public:
	// 构造函数
	CodeGenerator(Module * _module);

	// 析构函数
	virtual ~CodeGenerator() = default;

	// 代码产生器运行，结果保存到指定的文件中
	bool run(std::string outFileName);

	// 设置是否显示IR指令内容
	void setShowLinearIR(bool show)
	{
		this->showLinearIR = show;
	}

protected:
	// 代码产生器运行，结果保存到指定的文件中
	virtual bool run() = 0;

	// 一个C语言的文件对应一个Module
	Module * module;

	// 输出文件指针
	FILE * fp = nullptr;

	// 显示IR指令内容
	bool showLinearIR = false;
};
