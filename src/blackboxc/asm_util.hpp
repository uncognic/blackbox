#pragma once

#include "../define.hpp"
#include <cstdint>
#include <cstdio>
#include <print>
#include <string>
#include <string_view>
#include <vector>


namespace bbxc::asm_helpers {

// string only
struct DataEntry {
    std::string name;
    std::string value;
    uint32_t index = 0;
};

struct Label {
    std::string name;
    uint32_t addr = 0;
    uint32_t frame_size = 0;
};

struct Macro {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> body;
};

std::string trim_copy(std::string_view s);
void trim_crlf(std::string& s);
int failf(std::FILE* in, std::FILE* out, std::string_view message);
void append_le_bytes(std::vector<uint8_t>& buf, uint64_t value, size_t width);
std::vector<std::string> split_tokens(std::string_view s);
int collect_lines_from_buffer(std::string_view src, std::vector<std::string>& lines);

template <typename... Args> void dbg(bool debug, std::format_string<Args...> fmt, Args&&... args) {
    if (debug) {
        std::print(fmt, std::forward<Args>(args)...);
    }
}

} // namespace bbxc::asm_helpers
