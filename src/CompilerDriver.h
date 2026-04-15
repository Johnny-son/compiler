#pragma once

#include <string>

#include "Status.h"

namespace compiler {

enum class OutputKind {
    Assembly,
    Ast,
    IR
};

struct DriverOptions {
    bool show_help = false;
    std::string input_file;
    std::string output_file;
    OutputKind output_kind = OutputKind::Assembly;
};

class CompilerDriver {
public:
    Status run(const DriverOptions &options) const;
    static std::string default_output_file(OutputKind kind);
    static std::string help_text(const std::string &exe_name);
};

} // namespace compiler
