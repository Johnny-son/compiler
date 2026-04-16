#pragma once

#include <list>
#include <string>

#include "Module.h"

struct Riscv64Inst {
	std::string text;
	bool label = false;
	bool dead = false;

	explicit Riscv64Inst(std::string line = "", bool isLabel = false) : text(std::move(line)), label(isLabel)
	{}
};

class ILocRiscv64 {

	Module * module = nullptr;

	std::list<Riscv64Inst *> code;

	void emit(const std::string & line, bool isLabel = false);

	void load_imm(int rs_reg_no, int num);

	void load_symbol(int rs_reg_no, const std::string & name);

	void leaStack(int rs_reg_no, int base_reg_no, int offset);

public:
	explicit ILocRiscv64(Module * _module);

	~ILocRiscv64();

	void comment(const std::string & str);

	std::string toStr(int num, bool flag = true);

	std::list<Riscv64Inst *> & getCode();

	void load_base(int rs_reg_no, int base_reg_no, int disp);

	void store_base(int src_reg_no, int base_reg_no, int disp, int tmp_reg_no);

	void label(const std::string & name);

	void inst(const std::string & op);

	void inst(const std::string & op, const std::string & rs);

	void inst(const std::string & op, const std::string & rs, const std::string & arg1);

	void inst(const std::string & op, const std::string & rs, const std::string & arg1, const std::string & arg2);

	void load_var(int rs_reg_no, Value * var);

	void lea_var(int rs_reg_no, Value * var);

	void store_var(int src_reg_no, Value * var, int addr_reg_no);

	void mov_reg(int rs_reg_no, int src_reg_no);

	void call_fun(const std::string & name);

	void allocStack(Function * func, int tmp_reg_no);

	void ldr_args(Function * fun);

	void nop();

	void jump(const std::string & label);

	void outPut(FILE * file, bool outputEmpty = false);

	void deleteUnusedLabel();
};
