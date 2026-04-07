#pragma once

#include "../define.hpp"
#include <cstddef>
#include <cstdint>

namespace blackbox {
namespace tools {

uint32_t find_label(const char* name, Label* labels, size_t count);
uint32_t find_data(const char* name, Data* data, size_t count);

} // namespace tools
} // namespace blackbox
