#pragma once

#include <memory>
#include <string>
#include <vector>

namespace compiler::frontend {

enum class AstNodeKind {
    TranslationUnit,
    Placeholder
};

struct AstNode {
    AstNodeKind kind;
    std::string text;
    std::vector<std::unique_ptr<AstNode>> children;

    explicit AstNode(AstNodeKind node_kind, std::string node_text = {});
};

using AstNodePtr = std::unique_ptr<AstNode>;

} // namespace compiler::frontend
