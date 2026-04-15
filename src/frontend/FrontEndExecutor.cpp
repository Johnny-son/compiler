#include "FrontEndExecutor.h"

namespace compiler::frontend {

FrontEndExecutor::FrontEndExecutor(std::string input_file) : input_file_(std::move(input_file))
{}

const AstNode *FrontEndExecutor::ast_root() const
{
    return ast_root_.get();
}

} // namespace compiler::frontend
