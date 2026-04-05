#include <string>
#include <cerrno>
#include <cstdarg>
#include <print>

namespace blackbox {
namespace fmt {
std::string fmt_to_string(const char *fmt, va_list args)
{
    va_list copied;
    va_copy(copied, args);
    int needed = std::vsnprintf(nullptr, 0, fmt, copied);
    va_end(copied);
    if (needed <= 0)
        return {};

    std::string out(static_cast<size_t>(needed), '\0');
    std::vsnprintf(out.data(), out.size() + 1, fmt, args);
    return out;
}

void out_fmt(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::string text = fmt_to_string(fmt, args);
    va_end(args);
    std::print("{}", text);
}

void err_fmt(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::string text = fmt_to_string(fmt, args);
    va_end(args);
    std::print(stderr, "{}", text);
}

void err_errno(const char *prefix)
{
    std::error_code code(errno, std::generic_category());
    err_fmt("%s: %s\n", prefix, code.message().c_str());
}

} // namespace fmt
}