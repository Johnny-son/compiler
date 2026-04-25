// 二元操作指令

#include "BinaryInstruction.h"

/// @brief 构造函数
/// @param _op 操作符
/// @param _result 结果操作数
/// @param _srcVal1 源操作数1
/// @param _srcVal2 源操作数2
BinaryInstruction::BinaryInstruction(
	Function * _func, IRInstOperator _op, Value * _srcVal1, Value * _srcVal2, Type * _type)
	: Instruction(_func, _op, _type)
{
	addOperand(_srcVal1);
	addOperand(_srcVal2);
}

/// @brief 转换成字符串
/// @param str 转换后的字符串
void BinaryInstruction::toString(std::string & str)
{

	Value *src1 = getOperand(0), *src2 = getOperand(1);

	switch (op) {
		case IRInstOperator::IRINST_OP_ADD_I:

			// 加法指令，二元运算
			str = getIRName() + " = add " + src1->getIRName() + "," + src2->getIRName();
			break;
		case IRInstOperator::IRINST_OP_SUB_I:

			// 减法指令，二元运算
			str = getIRName() + " = sub " + src1->getIRName() + "," + src2->getIRName();
			break;
		case IRInstOperator::IRINST_OP_MUL_I:

			// 乘法指令，二元运算
			str = getIRName() + " = mul " + src1->getIRName() + "," + src2->getIRName();
			break;
			case IRInstOperator::IRINST_OP_DIV_I:

				// DragonIR 的整数除法关键字是 div
				str = getIRName() + " = div " + src1->getIRName() + "," + src2->getIRName();
				break;
		case IRInstOperator::IRINST_OP_MOD_I:

				// DragonIR 的整数求余关键字是 mod
				str = getIRName() + " = mod " + src1->getIRName() + "," + src2->getIRName();
				break;
		case IRInstOperator::IRINST_OP_CMP_EQ_I:
			// 比较指令统一输出成cmp_*，结果仍按i32 0/1约定给上层使用
			str = getIRName() + " = cmp_eq " + src1->getIRName() + "," + src2->getIRName();
			break;
		case IRInstOperator::IRINST_OP_CMP_NE_I:
			// 不等比较
			str = getIRName() + " = cmp_ne " + src1->getIRName() + "," + src2->getIRName();
			break;
		case IRInstOperator::IRINST_OP_CMP_LT_I:
			// 小于比较
			str = getIRName() + " = cmp_lt " + src1->getIRName() + "," + src2->getIRName();
			break;
		case IRInstOperator::IRINST_OP_CMP_LE_I:
			// 小于等于比较
			str = getIRName() + " = cmp_le " + src1->getIRName() + "," + src2->getIRName();
			break;
		case IRInstOperator::IRINST_OP_CMP_GT_I:
			// 大于比较
			str = getIRName() + " = cmp_gt " + src1->getIRName() + "," + src2->getIRName();
			break;
		case IRInstOperator::IRINST_OP_CMP_GE_I:
			// 大于等于比较
			str = getIRName() + " = cmp_ge " + src1->getIRName() + "," + src2->getIRName();
			break;

		default:
			// 未知指令
			Instruction::toString(str);
			break;
	}
}
