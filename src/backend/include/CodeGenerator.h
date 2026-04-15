#pragma once

#include <string>

#include "Module.h"
#include "Status.h"

namespace compiler::backend {

class CodeGenerator {
public:
    explicit CodeGenerator(const ir::Module *module);
    virtual ~CodeGenerator() = default;

    Status run(const std::string &output_file);

protected:
    virtual Status emit_to_file(const std::string &output_file) = 0;

    const ir::Module *module_;
};

} // namespace compiler::backend
