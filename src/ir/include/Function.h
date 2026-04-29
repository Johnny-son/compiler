// 函数头文件

#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "GlobalValue.h"
#include "ir/Types/FunctionType.h"
#include "ir/Values/FormalParam.h"

class BasicBlock;

///
/// @brief 描述函数信息的类，是全局静态存储，其Value的类型为FunctionType
///
class Function : public GlobalValue {

public:
	/// @brief 指定函数名字、函数返回类型以及函数形式参数的构造函数
	/// @param _name
	/// @param _type
	/// @param _param
	explicit Function(std::string _name, FunctionType * _type, bool _builtin = false);

	///
	/// @brief 析构函数
	/// @brief 释放函数占用的内存和IR指令代码
	/// @brief 注意：IR指令代码并未释放，需要手动释放
	~Function();

	/// @brief 获取函数返回类型
	/// @return 返回类型
	Type * getReturnType();

	/// @brief 获取函数的形参列表
	/// @return 形参列表
	std::vector<FormalParam *> & getParams();

	/// @brief 判断该函数是否是内置函数
	/// @return true: 内置函数，false：用户自定义
	bool isBuiltin();

	/// @brief 函数指令信息输出
	/// @param str 函数指令
	void toString(std::string & str);

	/// @brief 创建基本块
	BasicBlock * createBlock(const std::string & name = "");

	/// @brief 获取入口基本块
	BasicBlock * getEntryBlock() const;

	/// @brief 获取基本块列表
	const std::vector<BasicBlock *> & getBasicBlocks() const;

	/// @brief 分配函数内唯一的LLVM局部名字
	std::string allocateLocalName(const std::string & hint = "");

	///
	/// @brief  检查是否是函数
	/// @return true 是函数
	/// @return false 不是函数
	///
	[[nodiscard]] bool isFunction() const override
	{
		return true;
	}

	/// @brief 获取函数调用参数个数的最大值
	/// @return 函数调用参数个数的最大值
	int getMaxFuncCallArgCnt();

	/// @brief 设置函数调用参数个数的最大值
	/// @param count 函数调用参数个数的最大值
	void setMaxFuncCallArgCnt(int count);

	/// @brief 清理函数内申请的资源
	void Delete();

	///
	/// @brief 函数内的Value重命名，用于IR指令的输出
	///
	void renameIR();

private:
	///
	/// @brief 函数的返回值类型，有点冗余，可删除，直接从type中取得即可
	///
	Type * returnType;

	///
	/// @brief 形式参数列表
	///
	std::vector<FormalParam *> params;

	///
	/// @brief 是否是内置函数或者外部库函数
	///
	bool builtIn = false;

	/// @brief LLVM风格基本块列表
	std::vector<BasicBlock *> basicBlocks;

	/// @brief LLVM局部SSA名字计数器
	int32_t nameCounter = 0;

	/// @brief 已分配的LLVM局部名字
	std::unordered_set<std::string> usedLocalNames;

	/// @brief 已分配的LLVM基本块名字
	std::unordered_set<std::string> usedBlockNames;

	///
	/// @brief 本函数内函数调用的参数个数最大值
	///
	int maxFuncCallArgCnt = 0;
};
