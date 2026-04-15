#include <iostream>
#include <string>
#include <getopt.h>
#include "utils/Status.h"
#include "frontend/include/AST.h"
#include "frontend/include/ASTGenerator.h"
#include "frontend/include/Graph.h"
#include "ir/include/Module.h"
#include "ir/include/IRGenerator.h"
// #include "backend/include/CodeGenerator.h"

using namespace std;

// 参数配置
static bool gShowHelp       = false;  // 是否显示帮助信息
static bool gShowAST        = false;  // 显示抽象语法树，非线性IR
static bool gShowLineIR     = false;  // 产生线性IR，线性IR，默认输出
static bool gShowASM        = false;  // 显示汇编
static bool gShowSymbol     = false;  // 输出中间IR，含汇编或者自定义IR等，默认输出线性IR
static bool gFrontEndAntlr4 = true;  // 前端分析器Antlr4，是否选中
static bool gAsmAlsoShowIR  = false;  // 在输出汇编时是否输出中间IR作为注释
static int  gOptLevel       = 0;      // 优化的级别，即-O后面的数字，默认为0
static string gCPUTarget    = "RISCV64";  // 指定CPU目标架构，这里默认为RISCV64

static string gInputFile;   // 输入源文件
static string gOutputFile;  // 输出文件

static struct option long_options[] = {
	{"help", no_argument, nullptr, 'h'},
	{"output", required_argument, nullptr, 'o'},
	{"symbol", no_argument, nullptr, 'S'},
	{"ast", no_argument, nullptr, 'T'},
	{"ir", no_argument, nullptr, 'I'},
	{"llvm-ir", no_argument, nullptr, 'L'},
	{"antlr4", no_argument, nullptr, 'A'},
	{"optimize", required_argument, nullptr, 'O'},
	{"target", required_argument, nullptr, 't'},
	{"asmir", no_argument, nullptr, 'c'},
	{nullptr, 0, nullptr, 0}
};


static void showHelp(const std::string & exeName)
{
	std::cout << exeName + " -S [--symbol] [-A | --antlr4 | -D | --recursive-descent] [-T | --ast | -I | --ir] [-o "
						   "output | --output=output] source\n";
	std::cout << "Options:\n";
	std::cout << "  -h, --help                 Show this help message\n";
	std::cout << "  -o, --output=FILE          Specify output file\n";
	std::cout << "  -S, --symbol               Show symbol information\n";
	std::cout << "  -T, --ast                  Output abstract syntax tree\n";
	std::cout << "  -I, -L, --ir, --llvm-ir    Output LLVM IR\n";
	std::cout << "  -A, --antlr4               Use Antlr4 for lexical and syntax analysis\n";
	std::cout << "  -O, --optimize=LEVEL       Set optimization level\n";
	std::cout << "  -t, --target=CPU           Specify target CPU architecture\n";
	std::cout << "  -c, --asmir                Show IR instructions as comments in assembly output\n";
}

// 参数解析
static int ArgsAnalysis(int argc, char * argv[]) {
	// 指定参数解析的选项，可识别-h、-o、-S、-T、-I、-A、-D等选项
	// -S 必须项，输出中间IR、抽象语法树或汇编
	// -T 指定时输出AST, -I输出中间IR, 不指定则默认输出汇编
	// -A 指定按照antlr4进行词法与语法分析, 可有可无
	// -o 要求必须带有附加参数, 指定输出的文件
	// -O 要求必须带有附加整数, 指明优化的级别
	// -t 要求必须带有目标CPU, 指明目标CPU的汇编
	// -c 选项在输出汇编时有效, 附带输出IR指令内容

	int ch;  // 临时保存解析到的选项字符
	const char options[] = "ho:STILAO:t:c";
	int option_index = 0;  // getopt_long 内部用来记录当前匹配到了哪个长选项

	opterr = 1;  // 开启 getopt 的报错功能. 如果用户输入了不支持的选项, 会自动打印错误信息.

lb_check:
	while((ch = getopt_long(argc, argv, options, long_options, &option_index)) != -1) {
		switch (ch) {
			case 'h':
				gShowHelp = true; break;
			case 'o':
				gOutputFile = optarg; break;
			case 'S':
				gShowSymbol = true; break;
			case 'T':
				gShowAST = true; break;
			case 'I':
			case 'L':
				gShowLineIR = true;  // 产生中间IR
				break;
				break;
			case 'A':
				gFrontEndAntlr4 = true; break;  // 选用antlr4
			case 'O':
				// 优化级别分析，暂时没有用，如开启优化时请使用
				gOptLevel = std::stoi(optarg); break;
			case 't':
				gCPUTarget = optarg; break;
			case 'c':
				gAsmAlsoShowIR = true; break;
			default:
				return -1;
				break; /* no break */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc >= 1) {
		if (gInputFile.empty()) {
            gInputFile = argv[0]; // 把第一个剩余参数当作输入源文件
        } else {
            // 如果 gInputFile 已经有值了，说明用户输入了多个源文件，但这个编译器目前只支持一个，所以报错返回 -1。
            return -1;
        }

        if (argc > 1) {
            // 如果源文件后面还跟着其他参数（可能是把选项写在文件名后面了）
            optind = 0; // 重置 getopt 的内部状态
            goto lb_check; // 跳回到前面的 lb_check 标签，继续用 getopt_long 循环解析剩下的参数
        }
	}

	if (gInputFile.empty()) {
        return -1;
    }

    // 显示符号信息，必须指定，可选抽象语法树、中间IR(DragonIR)等显示
    if (!gShowSymbol) {
        return -1;
    }

	// 检查是否有冲突的选项
    int flag = (int) gShowLineIR + (int) gShowAST;

    if (flag == 0) {
        // 两者都没选，默认输出汇编代码
        gShowASM = true;
    } else if (flag != 1) {
        // 如果 flag > 1，说明 gShowLineIR 和 gShowAST 都为 true。
        // 即用户同时输入了 -I 和 -T，这两个输出模式冲突，所以报错返回 -1。
        return -1;
    }

	if (gOutputFile.empty()) {
        if (gShowAST) {
            gOutputFile = "output.png";
        } else if (gShowLineIR) {
            gOutputFile = "output.ll";
        } else {
            gOutputFile = "output.s";
        }
    }

	return 0;
}

// 编译执行
static int compile(string inputFile, string outputFile)
{
	// 函数返回值, 默认-1
	int result = -1;

	Module * module = nullptr;

	// 这里采用do {} while(0)架构的目的是如果处理出错可通过break退出循环, 出口唯一
	// 在编译器编译优化时会自动去除, 因为while恒假的缘故
	do {

		// 编译过程主要包括:
		// (1）词法语法分析生成AST
		// (2) 遍历AST生成线性IR
		// (3) 对线性IR进行优化：目前不支持
		// (4) 把线性IR转换成汇编



		// ===前端执行：词法分析、语法分析后产生抽象语法树，其root为全局变量ast_root===
		ASTGenerator * ast_generator = nullptr;
		ast_generator = new ASTGenerator(inputFile);  // Antlr4

		if (!ast_generator->run()) {
			Status::Error("前端分析错误");
			break;
		}

		// 获取抽象语法树的根节点
		ast_node * astRoot = ast_generator->getASTRoot();
		delete ast_generator;  // 清理前端资源

		// 这里可进行非线性AST的优化
		// ......

		if (gShowAST) {
			OutputAST(astRoot, outputFile);  // 遍历抽象语法树，生成抽象语法树图片
			ast_node::Delete(astRoot);  // 清理抽象语法树
			result = 0;  // 设置返回结果：正常
			break;
		}
		// ===前端完成===



		// ===中端执行: 遍历AST转换成线性IR指令===

		// 符号表，保存所有的变量以及函数等信息
		Module * module = new Module(inputFile);

		// 遍历抽象语法树产生线性IR，相关信息保存到符号表中
		IRGenerator * ir_generator = nullptr;
		ir_generator = new IRGenerator(astRoot, module);
		
		if (!ir_generator->run()) {
			Status::Error( "中间IR生成错误");
			break;
		}

		// 清理抽象语法树
		ast_node::Delete(astRoot);

		if (gShowLineIR) {
			module->renameIR();  // 对IR的名字重命名
			module->outputIR(outputFile);  // 输出IR
			result = 0;  // 设置返回结果：正常
			break;
		}

		// 要使得汇编能输出IR指令作为注释，必须对IR的名字进行命名，否则为空值
		if (gAsmAlsoShowIR) {
			module->renameIR();  // 对IR的名字重命名
		}

		// 这里可追加中间代码优化，体系结果无关的优化等
		// ......

		// ===中端完成===



		// ===后端执行，体系结果相关的操作===
		// if (gShowASM) {

		// 	CodeGenerator * code_generator = nullptr;

		// 	if (gCPUTarget == "RISCV64") {
		// 		// 输出面向ARM32的汇编指令
		// 		code_generator = new CodeGeneratorRiscv64(module);
		// 		code_generator->setShowLinearIR(gAsmAlsoShowIR);
		// 		code_generator->run(outputFile);
		// 	} else {
		// 		// 不支持指定的CPU架构
		// 		Status::Error("指定的目标CPU架构(%s)不支持", gCPUTarget.c_str());
		// 		break;
		// 	}

		// 	delete code_generator;
		// }

		// 清理符号表
		module->Delete();

		// 成功执行
		result = 0;

	} while (false);

	delete module;

	return result;
}

int main(int argc, char *argv[]) {
	// 参数解析
	if (ArgsAnalysis(argc, argv) < 0) {
		Status::Error("参数解析失败");
		return -1;
	}

	// 参数解析正确，进行编译处理，目前只支持一个文件的编译。
	int result = compile(gInputFile, gOutputFile);

	return result;

}
