#pragma once
#include <string>
#include <cerrno>
#include <cstdarg>
#include <print>

namespace blackbox {
namespace fmt {
std::string fmt_to_string(const char *fmt, va_list args);
void out_fmt(const char *fmt, ...);
void err_fmt(const char *fmt, ...);
void err_errno(const char *prefix);
} // namespace fmt
} // namespace bbxc