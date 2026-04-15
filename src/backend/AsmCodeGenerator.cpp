#include "AsmCodeGenerator.h"

#include <fstream>

namespace compiler::backend {

AsmCodeGenerator::AsmCodeGenerator(const ir::Module *module) : CodeGenerator(module)
{}

Status AsmCodeGenerator::emit_to_file(const std::string &output_file)
{
    std::ofstream output(output_file, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        return compiler::Status::Error("failed to open output file: " + output_file);
    }

    output << ".text\n";
    output << ".global main\n";
    output << "main:\n";
    output << "    movl $0, %eax\n";
    output << "    ret\n";

    for (const auto &instruction : module_->instructions()) {
        output << "# " << instruction << '\n';
    }

    return compiler::Status::Ok();
}

} // namespace compiler::backend
