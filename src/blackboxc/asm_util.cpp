#include "asm_util.hpp"
#include <cctype>
#include <print>


namespace bbxc::asm_helpers {

std::string trim_copy(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }
    size_t end = s.size();
    while (end > start && isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }
    return std::string(s.substr(start, end - start));
}

void trim_crlf(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
}

int failf(std::FILE* in, std::FILE* out, std::string_view message) {
    std::println(stderr, "{}", message);
    if (in)  std::fclose(in);
    if (out) std::fclose(out);
    return 1;
}

void append_le_bytes(std::vector<uint8_t>& buf, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; i++) {
        buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

std::vector<std::string> split_tokens(std::string_view s) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) {
            i++;
        }
        if (i >= s.size()) break;
        size_t start = i;
        while (i < s.size() && !isspace(static_cast<unsigned char>(s[i])) && s[i] != ',') {
            i++;
        }
        tokens.emplace_back(s.substr(start, i - start));
    }
    return tokens;
}

int collect_lines_from_buffer(std::string_view src, std::vector<std::string>& lines) {
    size_t start = 0;
    while (start < src.size()) {
        size_t nl = src.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.emplace_back(src.substr(start));
            break;
        }
        lines.emplace_back(src.substr(start, nl - start + 1));
        start = nl + 1;
    }
    return 0;
}

} // namespace bbxc::asm_helpers
