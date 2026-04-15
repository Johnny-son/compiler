#include "CompilerDriver.h"

#include <fstream>

#include "AsmCodeGenerator.h"
#include "FrontEndExecutor.h"
#include "IRGenerator.h"
#include "Module.h"
#include "RecursiveDescentExecutor.h"

namespace compiler {

namespace {

Status write_text_file(const std::string &output_file, const std::string &content)
{
    std::ofstream output(output_file, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        return Status::Error("failed to open output file: " + output_file);
    }

    output << content;
    return Status::Ok();
}

std::string render_ast(const frontend::AstNode *root)
{
    if (root == nullptr) {
        return "<empty ast>\n";
    }

    std::string text = "TranslationUnit";
    if (!root->text.empty()) {
        text += ": " + root->text;
    }
    text += "\n";
    return text;
}

std::string render_ir(const ir::Module &module)
{
    std::string text;
    for (const auto &instruction : module.instructions()) {
        text += instruction + "\n";
    }
    return text;
}

} // namespace

Status CompilerDriver::run(const DriverOptions &options) const
{
    frontend::RecursiveDescentExecutor frontend(options.input_file);
    Status status = frontend.run();
    if (!status.ok()) {
        return status;
    }

    ir::Module module(options.input_file);
    ir::IRGenerator ir_generator(frontend.ast_root(), &module);
    status = ir_generator.run();
    if (!status.ok()) {
        return status;
    }

    switch (options.output_kind) {
    case OutputKind::Ast:
        return write_text_file(options.output_file, render_ast(frontend.ast_root()));
    case OutputKind::IR:
        return write_text_file(options.output_file, render_ir(module));
    case OutputKind::Assembly: {
        backend::AsmCodeGenerator generator(&module);
        return generator.run(options.output_file);
    }
    }

    return Status::Error("unknown output kind");
}

std::string CompilerDriver::default_output_file(OutputKind kind)
{
    switch (kind) {
    case OutputKind::Ast:
        return "output.ast";
    case OutputKind::IR:
        return "output.ir";
    case OutputKind::Assembly:
        return "output.s";
    }

    return "output.txt";
}

std::string CompilerDriver::help_text(const std::string &exe_name)
{
    return "Usage: " + exe_name + " -S [-T | -I] [-o output] source\n"
           "  -h, --help      Show this help message\n"
           "  -S              Enable compilation pipeline\n"
           "  -T              Output AST placeholder\n"
           "  -I              Output IR placeholder\n"
           "  -o <file>       Write result to file\n";
}

} // namespace compiler
