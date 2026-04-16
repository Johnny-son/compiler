#include <cstdio>
#include <string>

#include "Function.h"
#include "ILocRiscv64.h"
#include "PlatformRiscv64.h"
#include "ir/Values/ConstInt.h"
#include "ir/Values/GlobalVariable.h"
#include "utils/Status.h"

namespace {

int slotSizeForType(Type * type)
{
	if ((type != nullptr) && type->isPointerType()) {
		return 8;
	}

	if (type != nullptr) {
		const int size = type->getSize();
		if (size > 0) {
			return size;
		}
	}

	return 4;
}

const char * loadOpForType(Type * type)
{
	return slotSizeForType(type) > 4 ? "ld" : "lw";
}

const char * storeOpForType(Type * type)
{
	return slotSizeForType(type) > 4 ? "sd" : "sw";
}

std::string formatMemory(const std::string & base, int offset)
{
	return std::to_string(offset) + "(" + base + ")";
}

} // namespace

ILocRiscv64::ILocRiscv64(Module * _module) : module(_module)
{}

ILocRiscv64::~ILocRiscv64()
{
	for (auto * inst: code) {
		delete inst;
	}
}

void ILocRiscv64::emit(const std::string & line, bool isLabel)
{
	code.push_back(new Riscv64Inst(line, isLabel));
}

void ILocRiscv64::comment(const std::string & str)
{
	emit("# " + str);
}

std::string ILocRiscv64::toStr(int num, bool flag)
{
	if (!flag) {
		return std::to_string(num);
	}

	return std::to_string(num);
}

std::list<Riscv64Inst *> & ILocRiscv64::getCode()
{
	return code;
}

void ILocRiscv64::label(const std::string & name)
{
	emit(name + ":", true);
}

void ILocRiscv64::inst(const std::string & op)
{
	emit(op);
}

void ILocRiscv64::inst(const std::string & op, const std::string & rs)
{
	emit(op + " " + rs);
}

void ILocRiscv64::inst(const std::string & op, const std::string & rs, const std::string & arg1)
{
	emit(op + " " + rs + ", " + arg1);
}

void ILocRiscv64::inst(const std::string & op, const std::string & rs, const std::string & arg1, const std::string & arg2)
{
	emit(op + " " + rs + ", " + arg1 + ", " + arg2);
}

void ILocRiscv64::load_imm(int rs_reg_no, int constant)
{
	inst("li", PlatformRiscv64::regName[rs_reg_no], std::to_string(constant));
}

void ILocRiscv64::load_symbol(int rs_reg_no, const std::string & name)
{
	inst("la", PlatformRiscv64::regName[rs_reg_no], name);
}

void ILocRiscv64::load_base(int rs_reg_no, int base_reg_no, int offset)
{
	const std::string & rsReg = PlatformRiscv64::regName[rs_reg_no];
	const std::string & base = PlatformRiscv64::regName[base_reg_no];

	if (PlatformRiscv64::isDisp(offset)) {
		inst("lw", rsReg, formatMemory(base, offset));
		return;
	}

	load_imm(rs_reg_no, offset);
	inst("add", rsReg, base, rsReg);
	inst("lw", rsReg, formatMemory(rsReg, 0));
}

void ILocRiscv64::store_base(int src_reg_no, int base_reg_no, int disp, int tmp_reg_no)
{
	const std::string & src = PlatformRiscv64::regName[src_reg_no];
	const std::string & base = PlatformRiscv64::regName[base_reg_no];

	if (PlatformRiscv64::isDisp(disp)) {
		inst("sw", src, formatMemory(base, disp));
		return;
	}

	load_imm(tmp_reg_no, disp);
	inst("add", PlatformRiscv64::regName[tmp_reg_no], base, PlatformRiscv64::regName[tmp_reg_no]);
	inst("sw", src, formatMemory(PlatformRiscv64::regName[tmp_reg_no], 0));
}

void ILocRiscv64::mov_reg(int rs_reg_no, int src_reg_no)
{
	if (rs_reg_no != src_reg_no) {
		inst("mv", PlatformRiscv64::regName[rs_reg_no], PlatformRiscv64::regName[src_reg_no]);
	}
}

void ILocRiscv64::load_var(int rs_reg_no, Value * src_var)
{
	if (Instanceof(constVal, ConstInt *, src_var)) {
		load_imm(rs_reg_no, constVal->getVal());
		return;
	}

	if (src_var->getRegId() != -1) {
		mov_reg(rs_reg_no, src_var->getRegId());
		return;
	}

	if (Instanceof(globalVar, GlobalVariable *, src_var)) {
		load_symbol(rs_reg_no, globalVar->getName());
		inst(loadOpForType(src_var->getType()), PlatformRiscv64::regName[rs_reg_no], formatMemory(PlatformRiscv64::regName[rs_reg_no], 0));
		return;
	}

	int32_t baseRegId = -1;
	int64_t offset = 0;
	if (!src_var->getMemoryAddr(&baseRegId, &offset)) {
		Status::Error("load_var: unsupported operand");
		return;
	}

	const std::string & rsReg = PlatformRiscv64::regName[rs_reg_no];
	const std::string & base = PlatformRiscv64::regName[baseRegId];

	if (PlatformRiscv64::isDisp(static_cast<int>(offset))) {
		inst(loadOpForType(src_var->getType()), rsReg, formatMemory(base, static_cast<int>(offset)));
		return;
	}

	load_imm(rs_reg_no, static_cast<int>(offset));
	inst("add", rsReg, base, rsReg);
	inst(loadOpForType(src_var->getType()), rsReg, formatMemory(rsReg, 0));
}

void ILocRiscv64::lea_var(int rs_reg_no, Value * var)
{
	if (Instanceof(globalVar, GlobalVariable *, var)) {
		load_symbol(rs_reg_no, globalVar->getName());
		return;
	}

	int32_t baseRegId = -1;
	int64_t offset = 0;
	if (!var->getMemoryAddr(&baseRegId, &offset)) {
		Status::Error("lea_var: unsupported operand");
		return;
	}

	leaStack(rs_reg_no, baseRegId, static_cast<int>(offset));
}

void ILocRiscv64::store_var(int src_reg_no, Value * dest_var, int tmp_reg_no)
{
	if (dest_var->getRegId() != -1) {
		mov_reg(dest_var->getRegId(), src_reg_no);
		return;
	}

	if (Instanceof(globalVar, GlobalVariable *, dest_var)) {
		load_symbol(tmp_reg_no, globalVar->getName());
		inst(storeOpForType(dest_var->getType()), PlatformRiscv64::regName[src_reg_no], formatMemory(PlatformRiscv64::regName[tmp_reg_no], 0));
		return;
	}

	int32_t baseRegId = -1;
	int64_t offset = 0;
	if (!dest_var->getMemoryAddr(&baseRegId, &offset)) {
		Status::Error("store_var: unsupported operand");
		return;
	}

	const std::string & src = PlatformRiscv64::regName[src_reg_no];
	const std::string & base = PlatformRiscv64::regName[baseRegId];

	if (PlatformRiscv64::isDisp(static_cast<int>(offset))) {
		inst(storeOpForType(dest_var->getType()), src, formatMemory(base, static_cast<int>(offset)));
		return;
	}

	load_imm(tmp_reg_no, static_cast<int>(offset));
	inst("add", PlatformRiscv64::regName[tmp_reg_no], base, PlatformRiscv64::regName[tmp_reg_no]);
	inst(storeOpForType(dest_var->getType()), src, formatMemory(PlatformRiscv64::regName[tmp_reg_no], 0));
}

void ILocRiscv64::leaStack(int rs_reg_no, int base_reg_no, int off)
{
	const std::string & rs = PlatformRiscv64::regName[rs_reg_no];
	const std::string & base = PlatformRiscv64::regName[base_reg_no];

	if (PlatformRiscv64::constExpr(off)) {
		inst("addi", rs, base, std::to_string(off));
		return;
	}

	load_imm(rs_reg_no, off);
	inst("add", rs, base, rs);
}

void ILocRiscv64::allocStack(Function * func, int)
{
	const int frameSize = func->getMaxDep();
	if (frameSize <= 0) {
		return;
	}

	inst("addi", "sp", "sp", std::to_string(-frameSize));
	inst("sd", "ra", formatMemory("sp", frameSize - 8));
	inst("sd", "s0", formatMemory("sp", frameSize - 16));
	inst("addi", "s0", "sp", std::to_string(frameSize));
}

void ILocRiscv64::call_fun(const std::string & name)
{
	inst("call", name);
}

void ILocRiscv64::ldr_args(Function *)
{}

void ILocRiscv64::nop()
{
	inst("nop");
}

void ILocRiscv64::jump(const std::string & labelName)
{
	inst("j", labelName);
}

void ILocRiscv64::outPut(FILE * file, bool outputEmpty)
{
	for (auto * inst: code) {
		if (inst->dead) {
			continue;
		}

		if (inst->text.empty()) {
			if (outputEmpty) {
				fprintf(file, "\n");
			}
			continue;
		}

		if (inst->label) {
			fprintf(file, "%s\n", inst->text.c_str());
		} else {
			fprintf(file, "\t%s\n", inst->text.c_str());
		}
	}
}

void ILocRiscv64::deleteUnusedLabel()
{}
