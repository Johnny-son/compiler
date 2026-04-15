#include "RecursiveDescentExecutor.h"

#include <fstream>
#include <sstream>

namespace compiler::frontend {

RecursiveDescentExecutor::RecursiveDescentExecutor(const std::string &input_file)
    : FrontEndExecutor(input_file)
{}

Status RecursiveDescentExecutor::run()
{
    std::ifstream input(input_file_, std::ios::in);
    if (!input.is_open()) {
        return compiler::Status::Error("failed to open input file: " + input_file_);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    ast_root_ = std::make_unique<AstNode>(AstNodeKind::TranslationUnit, input_file_);
    ast_root_->children.push_back(std::make_unique<AstNode>(AstNodeKind::Placeholder, buffer.str()));
    return compiler::Status::Ok();
}

} // namespace compiler::frontend
