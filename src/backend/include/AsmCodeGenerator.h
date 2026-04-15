#pragma once

#include "CodeGenerator.h"

namespace compiler::backend {

class AsmCodeGenerator : public CodeGenerator {
public:
    explicit AsmCodeGenerator(const ir::Module *module);

protected:
    Status emit_to_file(const std::string &output_file) override;
};

} // namespace compiler::backend
