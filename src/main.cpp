#include <iostream>
#include <string>

#include "CompilerDriver.h"

namespace {

int print_help(const std::string &exe_name)
{
    std::cout << compiler::CompilerDriver::help_text(exe_name);
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    compiler::DriverOptions options;
    bool enable_pipeline = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            options.show_help = true;
        } else if (arg == "-S") {
            enable_pipeline = true;
        } else if (arg == "-T") {
            options.output_kind = compiler::OutputKind::Ast;
        } else if (arg == "-I") {
            options.output_kind = compiler::OutputKind::IR;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "missing argument for -o\n";
                return 1;
            }
            options.output_file = argv[++i];
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "unknown option: " << arg << '\n';
            return 1;
        } else if (options.input_file.empty()) {
            options.input_file = arg;
        } else {
            std::cerr << "unexpected positional argument: " << arg << '\n';
            return 1;
        }
    }

    if (options.show_help) {
        return print_help(argv[0]);
    }

    if (!enable_pipeline) {
        std::cerr << "missing required option: -S\n";
        return 1;
    }

    if (options.input_file.empty()) {
        std::cerr << "missing input file\n";
        return 1;
    }

    if (options.output_file.empty()) {
        options.output_file = compiler::CompilerDriver::default_output_file(options.output_kind);
    }

    compiler::CompilerDriver driver;
    const compiler::Status status = driver.run(options);
    if (!status.ok()) {
        std::cerr << status.message() << '\n';
        return 1;
    }

    return 0;
}
