#pragma once

#include <string>
#include <vector>

namespace compiler::ir {

class Module {
public:
    explicit Module(std::string name);

    const std::string &name() const;
    void add_instruction(std::string instruction);
    const std::vector<std::string> &instructions() const;

private:
    std::string name_;
    std::vector<std::string> instructions_;
};

} // namespace compiler::ir
