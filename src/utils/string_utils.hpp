#pragma once

#include <string>

namespace blackbox {
namespace tools {

int equals_ci(const char* a, const char* b);
int starts_with_ci(const char* s, const char* prefix);
char* trim(char* s);
std::string replace_all(const std::string& src, const std::string& find, const std::string& repl);

} // namespace tools
} // namespace blackbox
