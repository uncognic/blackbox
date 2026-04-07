#pragma once

#include <cstddef>
#include <cstdint>

namespace blackbox {
namespace tools {

size_t instr_size(const char* line);
uint8_t parse_register(const char* r, int lineno);
uint8_t parse_file(const char* r, int lineno);

} // namespace tools
} // namespace blackbox
