#pragma once

#include "../define.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace blackbox {
namespace tools {

uint32_t find_label(const std::string& name, Label* labels, size_t count);
uint32_t find_data(const std::string& name, Data* data, size_t count);

} // namespace tools
} // namespace blackbox
