#include "Status.h"

namespace compiler {

Status::Status(bool ok, std::string message) : ok_(ok), message_(std::move(message))
{}

Status Status::Ok()
{
    return Status(true, "");
}

Status Status::Error(std::string message)
{
    return Status(false, std::move(message));
}

bool Status::ok() const
{
    return ok_;
}

const std::string &Status::message() const
{
    return message_;
}

} // namespace compiler
