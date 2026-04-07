#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace blackbox {
namespace tools {

size_t instr_size(const char* line);
uint8_t parse_register(const std::string& r, int lineno);
uint8_t parse_file(const std::string& r, int lineno);

} // namespace tools
} // namespace blackbox
