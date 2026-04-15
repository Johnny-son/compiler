#include <fstream>
#include "antlr4-runtime.h"
#include "ASTGenerator.h"
#include "CSTVisitor.h"
#include "MiniCLexer.h"
#include "MiniCParser.h"
#include "utils/Status.h"

namespace {

class MiniCErrorListener final : public antlr4::BaseErrorListener {
public:
	bool hasError = false;

	void syntaxError(antlr4::Recognizer * /*recognizer*/,
					 antlr4::Token * /*offendingSymbol*/,
					 size_t line,
					 size_t charPositionInLine,
					 const std::string & msg,
					 std::exception_ptr /*e*/) override
	{
		hasError = true;
		(void) Status::Error("line %zu:%zu %s", line, charPositionInLine, msg.c_str());
	}
};

}  // namespace

// 构造函数
ASTGenerator::ASTGenerator(std::string _filename) : filename(_filename) {}

// 前端词法与语法解析生成AST
bool ASTGenerator::run()
{
	std::ifstream ifs;
	ifs.open(filename);
	if (!ifs.is_open()) {
		Status::Error("文件(%s)不能打开，可能不存在", filename.c_str());
		return false;
	}

	// antlr4的输入流类实例
	antlr4::ANTLRInputStream input{ifs};

	// 词法分析器实例
	MiniCLexer lexer{&input};

	// 词法分析器实例转化成记号(Token)流
	antlr4::CommonTokenStream tokenStream{&lexer};

	// 利用antlr4进行分析，从compUnit开始分析输入字符串
	MiniCParser parser{&tokenStream};
	MiniCErrorListener errorListener;
	lexer.removeErrorListeners();
	parser.removeErrorListeners();
	lexer.addErrorListener(&errorListener);
	parser.addErrorListener(&errorListener);

	// 从具体语法树的根结点进行深度优先遍历，生成抽象语法树
	auto cstRoot = parser.compUnit();
	if (!cstRoot || errorListener.hasError) {
		Status::Error("Antlr4的词语与语法分析错误");
		return false;
	}

	/// 新建遍历器对具体语法树进行分析，产生抽象语法树
	MiniCCSTVisitor visitor;

	// 遍历产生抽象语法树
	astRoot = visitor.run(cstRoot);

	return true;
}
