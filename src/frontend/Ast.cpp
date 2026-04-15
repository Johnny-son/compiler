#include "Ast.h"

namespace compiler::frontend {

AstNode::AstNode(AstNodeKind node_kind, std::string node_text)
    : kind(node_kind), text(std::move(node_text))
{}

} // namespace compiler::frontend
