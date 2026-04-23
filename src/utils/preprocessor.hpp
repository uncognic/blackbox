#pragma once

#include <string>
#include <unordered_map>

namespace blackbox {
namespace tools {

bool preprocess_includes(const std::string& input, std::string& out);
bool preprocess_defines(std::string& input, std::unordered_map<std::string, std::string>& defines);

} // namespace tools
} // namespace blackbox
