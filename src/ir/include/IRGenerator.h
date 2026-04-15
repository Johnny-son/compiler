#pragma once

#include "Ast.h"
#include "Module.h"
#include "Status.h"

namespace compiler::ir {

class IRGenerator {
public:
    IRGenerator(const frontend::AstNode *root, Module *module);

    Status run();

private:
    const frontend::AstNode *root_;
    Module *module_;
};

} // namespace compiler::ir
