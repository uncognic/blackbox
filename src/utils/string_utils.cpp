#include "string_utils.hpp"
#include <cctype>
#include <cstring>
#include <string>

namespace blackbox {
namespace tools {

static inline unsigned char ascii_upper(unsigned char c) {
    return static_cast<unsigned char>(toupper(c));
}

static bool equals_ci_str(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (ascii_upper(static_cast<unsigned char>(a[i])) != ascii_upper(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

static bool starts_with_ci_str(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); i++) {
        if (ascii_upper(static_cast<unsigned char>(s[i])) != ascii_upper(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

int equals_ci(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    return equals_ci_str(std::string(a), std::string(b)) ? 1 : 0;
}

int starts_with_ci(const char* s, const char* prefix) {
    if (!s || !prefix) {
        return 0;
    }
    return starts_with_ci_str(std::string(s), std::string(prefix)) ? 1 : 0;
}

char* trim(char* s) {
    while (isspace(static_cast<unsigned char>(*s))) {
        s++;
    }
    if (*s == '\0') {
        return s;
    }

    char* end = s + strlen(s) - 1;
    while (end >= s && (isspace(*end) || *end == '\r' || *end == '\n')) {
        *end-- = '\0';
    }
    return s;
}

std::string replace_all(const std::string& src, const std::string& find, const std::string& repl) {
    if (find.empty()) {
        return src;
    }

    std::string out(src);
    size_t pos = 0;
    while ((pos = out.find(find, pos)) != std::string::npos) {
        out.replace(pos, find.size(), repl);
        pos += repl.size();
    }

    return out;
}

} // namespace tools
} // namespace blackbox
