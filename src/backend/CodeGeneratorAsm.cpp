// 后端汇编代码生成器接口的实现

#include "CodeGenerator.h"
#include "CodeGeneratorAsm.h"
#include "Module.h"
#include "Function.h"

// 构造函数
CodeGeneratorAsm::CodeGeneratorAsm(Module * _module) : CodeGenerator(_module)
{}

// .text代码段，主要存放CPU指令，以函数为单位
void CodeGeneratorAsm::genCodeSection()
{
	// 重新设置为0
	labelIndex = 0;

	// 遍历所有的函数，以函数为单位，产生指令
	for (auto func: module->getFunctionList()) {

		if (!func->isBuiltin()) {

			// 针对func产生汇编指令
			genCodeSection(func);
		}
	}
}

// 产生汇编文件
bool CodeGeneratorAsm::run()
{
	// 产生头
	genHeader();

	// 产生数据段，含初始化和未初始化数据
	genDataSection();

	// 产生代码段，即CPU指令，以函数为单位
	genCodeSection();

	return true;
}
