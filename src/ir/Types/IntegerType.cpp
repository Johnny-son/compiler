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
