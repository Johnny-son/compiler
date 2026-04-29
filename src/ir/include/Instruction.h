// IR指令头文件

#pragma once

#include "User.h"

class Function;

///
/// @brief IR指令的基类, 指令自带值，也就是常说的临时变量
///
class Instruction : public User {

public:
	/// @brief 构造函数
	/// @param result
	explicit Instruction(Function * _func, Type * _type);

	/// @brief 析构函数
	virtual ~Instruction() = default;

	///
	/// @brief 转换成IR指令文本形式
	/// @param str IR指令文本
	///
	virtual void toString(std::string & str);

	/// @brief 是否是Dead指令
	bool isDead();

	/// @brief 设置指令是否是Dead指令
	/// @param _dead 是否是Dead指令，true：Dead, false: 非Dead
	void setDead(bool _dead = true);

	///
	/// @brief 获取当前指令所在函数
	/// @return Function* 函数对象
	///
	Function * getFunction();

	///
	/// @brief 检查指令是否有值
	/// @return true
	/// @return false
	///
	bool hasResultValue();

	/// @brief 是否为基本块终结指令
	virtual bool isTerminator() const;

	///
	/// @brief 获得分配的寄存器编号或ID
	/// @return int32_t 寄存器编号
	///
	int32_t getRegId() override
	{
		return regId;
	}

	///
	/// @brief @brief 如是内存变量型Value，则获取基址寄存器和偏移
	/// @param regId 寄存器编号
	/// @param offset 相对偏移
	/// @return true 是内存型变量
	/// @return false 不是内存型变量
	///
	bool getMemoryAddr(int32_t * _regId = nullptr, int64_t * _offset = nullptr) override
	{
		// 内存寻址时，必须要指定基址寄存器

		// 没有指定基址寄存器则返回false
		if (this->baseRegNo == -1) {
			return false;
		}

		// 设置基址寄存器
		if (_regId) {
			*_regId = this->baseRegNo;
		}

		// 设置偏移
		if (_offset) {
			*_offset = this->offset;
		}

		return true;
	}

	///
	/// @brief 设置内存寻址的基址寄存器和偏移
	/// @param _regId 基址寄存器编号
	/// @param _offset 偏移
	///
	void setMemoryAddr(int32_t _regId, int64_t _offset)
	{
		baseRegNo = _regId;
		offset = _offset;
	}

	///
	/// @brief 对该Value进行Load用的寄存器编号
	/// @return int32_t 寄存器编号
	///
	int32_t getLoadRegId() override
	{
		return this->loadRegNo;
	}

	///
	/// @brief 对该Value进行Load用的寄存器编号
	/// @return int32_t 寄存器编号
	///
	void setLoadRegId(int32_t regId) override
	{
		this->loadRegNo = regId;
	}

protected:
	///
	/// @brief 是否是Dead指令
	///
	bool dead = false;

	///
	/// @brief 当前指令属于哪个函数
	///
	Function * func = nullptr;

	///
	/// @brief 寄存器编号，-1表示没有分配寄存器，大于等于0代表是寄存器型Value
	///
	int32_t regId = -1;

	///
	/// @brief 变量在栈内的偏移量，对于全局变量默认为0，临时变量没有意义
	///
	int32_t offset = 0;

	///
	/// @brief 栈内寻找时基址寄存器编号
	///
	int32_t baseRegNo = -1;

	///
	/// @brief 栈内寻找时基址寄存器名字
	///
	std::string baseRegName;

	///
	/// @brief 变量加载到寄存器中时对应的寄存器编号
	///
	int32_t loadRegNo = -1;
};
