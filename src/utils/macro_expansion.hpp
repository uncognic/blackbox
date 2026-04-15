#pragma once

#include "../define.hpp"
#include <cstdio>

namespace blackbox {
namespace tools {

Macro* find_macro(Macro* macros, size_t macro_count, const char* name);
int expand_invocation(const char* invocation_line, FILE* dest, int depth, Macro* macros,
                      size_t macro_count, unsigned long* expand_id);

} // namespace tools
} // namespace blackbox
