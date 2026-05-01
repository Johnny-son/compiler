// float类型类

#include "FloatType.h"

FloatType * FloatType::getTypeFloat()
{
	static FloatType * oneInstanceFloat = new FloatType();
	return oneInstanceFloat;
}
