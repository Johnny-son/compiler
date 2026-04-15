#include "Module.h"

namespace compiler::ir {

Module::Module(std::string name) : name_(std::move(name))
{}

const std::string &Module::name() const
{
    return name_;
}

void Module::add_instruction(std::string instruction)
{
    instructions_.push_back(std::move(instruction));
}

const std::vector<std::string> &Module::instructions() const
{
    return instructions_;
}

} // namespace compiler::ir
