#pragma once
#include <cstdarg>
#include <string>


namespace blackbox {
namespace fmt {
std::string fmt_to_string(const char* fmt, va_list args);
void out_fmt(const char* fmt, ...);
void err_fmt(const char* fmt, ...);
void err_errno(const char* prefix);
} // namespace fmt
} // namespace blackbox