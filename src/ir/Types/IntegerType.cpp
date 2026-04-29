// 整型类型类，可描述1位的bool类型或32位的int类型

#include "IntegerType.h"

///
/// @brief 获取类型bool
/// @return VoidType*
///
IntegerType * IntegerType::getTypeBool()
{
	static IntegerType * oneInstanceBool = new IntegerType(1);
	return oneInstanceBool;
}

///
/// @brief 获取类型int
/// @return VoidType*
///
IntegerType * IntegerType::getTypeInt()
{
	static IntegerType * oneInstanceInt = new IntegerType(32);
	return oneInstanceInt;
}
