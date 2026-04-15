#include "IRGenerator.h"

namespace compiler::ir {

IRGenerator::IRGenerator(const frontend::AstNode *root, Module *module) : root_(root), module_(module)
{}

Status IRGenerator::run()
{
    if (root_ == nullptr) {
        return compiler::Status::Error("AST root is null");
    }

    if (module_ == nullptr) {
        return compiler::Status::Error("IR module is null");
    }

    module_->add_instruction("; module: " + module_->name());
    module_->add_instruction("; TODO: lower AST to real IR");
    module_->add_instruction("ret 0");
    return compiler::Status::Ok();
}

} // namespace compiler::ir
