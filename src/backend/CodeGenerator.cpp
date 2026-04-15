#include "CodeGenerator.h"

namespace compiler::backend {

CodeGenerator::CodeGenerator(const ir::Module *module) : module_(module)
{}

Status CodeGenerator::run(const std::string &output_file)
{
    if (module_ == nullptr) {
        return compiler::Status::Error("code generation requires a valid IR module");
    }

    return emit_to_file(output_file);
}

} // namespace compiler::backend
