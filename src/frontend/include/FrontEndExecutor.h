#pragma once

#include <string>

#include "Ast.h"
#include "Status.h"

namespace compiler::frontend {

class FrontEndExecutor {
public:
    explicit FrontEndExecutor(std::string input_file);
    virtual ~FrontEndExecutor() = default;

    virtual Status run() = 0;
    const AstNode *ast_root() const;

protected:
    std::string input_file_;
    AstNodePtr ast_root_;
};

} // namespace compiler::frontend
