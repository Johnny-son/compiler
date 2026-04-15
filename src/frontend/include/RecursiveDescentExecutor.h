#pragma once

#include "FrontEndExecutor.h"

namespace compiler::frontend {

class RecursiveDescentExecutor : public FrontEndExecutor {
public:
    explicit RecursiveDescentExecutor(const std::string &input_file);

    Status run() override;
};

} // namespace compiler::frontend
