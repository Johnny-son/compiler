#include "Status.h"

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <utility>
#include <vector>

namespace {

std::string format_message(const char *format, va_list args)
{
    if (format == nullptr) {
        return "";
    }

    va_list args_copy;
    va_copy(args_copy, args);
    const int message_size = std::vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    if (message_size < 0) {
        return format;
    }

    std::vector<char> buffer(static_cast<std::size_t>(message_size) + 1);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    return std::string(buffer.data(), static_cast<std::size_t>(message_size));
}

} // namespace

namespace compiler {

Status::Status(bool ok, std::string message) : ok_(ok), message_(std::move(message))
{
    // 如果是错误状态且消息不为空，直接输出到 stderr
    if (!ok_ && !message_.empty()) {
        std::cerr << message_ << std::endl;
    }
}

Status Status::Ok()
{
    return Status(true, "");
}

Status Status::Error(std::string message)
{
    return Status(false, std::move(message));
}

Status Status::Error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::string message = format_message(format, args);
    va_end(args);
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
