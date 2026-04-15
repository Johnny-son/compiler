#pragma once

#include <string>

namespace compiler {

class Status {
public:
    static Status Ok();
    static Status Error(std::string message);

    bool ok() const;
    const std::string &message() const;

private:
    Status(bool ok, std::string message);

    bool ok_;
    std::string message_;
};

} // namespace compiler
