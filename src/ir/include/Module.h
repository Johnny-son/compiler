// 符号表-模块类
#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "ir/Values/ConstInt.h"
#include "Type.h"
#include "ir/Values/GlobalVariable.h"
#include "Function.h"

class ScopeStack;

// 一个Module代表一个C语言的源文件
class Module {

public:
	// 构造函数
	// name  模块名
	Module(std::string _name);

	// 缺省的析构函数
	virtual ~Module() = default;

	// 输出IR代码, 返回 std::string IR代码字符串
	std::string toIRString();

	// 根据后端输出汇编代码, 返回 std::string 汇编代码字符串
	// 获取模块的名字
	[[nodiscard]] std::string getName() const
	{
		return name;
	}

	// 进入作用域，如进入函数体块、语句块等
	void enterScope();

	// 退出作用域，如退出函数体块、语句块等
	void leaveScope();

	// 在遍历抽象语法树的过程中，获取当前正在处理的函数。在函数外处理时返回空指针。
	Function * getCurrentFunction();

	// 设置当前正在处理的函数指针。函数外设置空指针
	void setCurrentFunction(Function * current);

	/// @brief 新建函数并放到函数列表中
	/// @param name 函数名
	/// @param returnType 返回值类型
	/// @param params 形参列表
	/// @param builtin 是否内置函数
	/// @return 新建的函数对象实例
	Function *
	newFunction(std::string name, Type * returnType, std::vector<FormalParam *> params = {}, bool builtin = false);

	// 根据函数名查找函数信息
	Function * findFunction(std::string name);

	// 获取全局变量列表，用于外部遍历全局变量
	std::vector<GlobalVariable *> & getGlobalVariables()
	{
		return globalVariableVector;
	}

	// 获得函数列表
	std::vector<Function *> & getFunctionList()
	{
		return funcVector;
	}

	// 新建一个整型数值的Value，并加入到符号表，用于后续释放空间
	ConstInt * newConstInt(int32_t intVal);

	// 新建变量型Value，会根据currentFunc的值进行判断创建全局或者局部变量
	// ! 该函数只有在AST遍历生成线性IR中使用，其它地方不能使用
	// lineno: 当前变量定义所在的行号，用于错误信息；传入 -1 表示无有效行号
	Value * newVarValue(Type * type, const std::string & name = "", int64_t lineno = -1);

	// 查找变量（全局变量或局部变量），会根据作用域栈进行逐级查找。
	/// ! 该函数只有在AST遍历生成线性IR中使用，其它地方不能使用
	///  name 变量ID
	/// return 指针有效则找到，空指针未找到
	Value * findVarValue(std::string name);

	// 清理Module中管理的所有信息资源
	void Delete();

	// 输出线性IR指令列表
	void outputIR(const std::string & filePath);

	// 对IR指令中没有名字的全部命名
	void renameIR();

protected:
	// 根据整数值获取当前符号
	ConstInt * findConstInt(int32_t val);

	// 新建全局变量，要求 name 必须有效，并且加入到全局符号表中。
	GlobalVariable * newGlobalVariable(Type * type, std::string name);

	// 根据变量名获取当前符号（只管理全局变量）
	GlobalVariable * findGlobalVariable(std::string name);

	// 直接插入函数到符号表中，不考虑现有的表中是否存在
	void insertFunctionDirectly(Function * func);

	// Value插入到符号表中
	void insertGlobalValueDirectly(GlobalVariable * val);

	// ConstInt插入到符号表中
	void insertConstIntDirectly(ConstInt * val);

private:
	// 模块名，也就是要编译的文件名
	std::string name;

	// 所有的类型，便于内存的释放
	std::vector<Type *> types;

	// 变量作用域栈
	ScopeStack * scopeStack;

	// 遍历抽象树过程中的当前处理函数
	Function * currentFunc = nullptr;

	// 函数映射表，函数名-函数，便于检索
	std::unordered_map<std::string, Function *> funcMap;

	//  函数列表
	std::vector<Function *> funcVector;

	// 变量名映射表，变量名-变量，只保存全局变量
	std::unordered_map<std::string, GlobalVariable *> globalVariableMap;

	// 只保存全局变量
	std::vector<GlobalVariable *> globalVariableVector;

	// 常量表
	std::unordered_map<int32_t, ConstInt *> constIntMap;
};
