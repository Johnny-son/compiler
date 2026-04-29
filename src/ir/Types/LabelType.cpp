// Label名称符号类

#include "LabelType.h"

///
/// @brief 获取类型
/// @return VoidType*
///
LabelType * LabelType::getType()
{
	static LabelType * oneInstance = new LabelType();
	return oneInstance;
}
