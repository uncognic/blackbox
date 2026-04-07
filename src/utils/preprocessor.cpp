#include "preprocessor.hpp"
#include "string_utils.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace blackbox {
namespace tools {

static std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && isspace((unsigned char) s[start])) {
        start++;
    }

    size_t end = s.size();
    while (end > start &&
           (isspace((unsigned char) s[end - 1]) || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        end--;
    }

    return s.substr(start, end - start);
}

static bool preprocess_includes_impl(const char* input, int depth, std::string& out) {
    if (depth > 32) {
        fprintf(stderr, "Error: include nesting too deep\n");
        return false;
    }

    const std::string input_path(input);
    const size_t sep = input_path.find_last_of("/\\");
    const std::string base_dir =
        (sep == std::string::npos) ? std::string() : input_path.substr(0, sep);

    FILE* in = fopen(input, "rb");
    if (!in) {
        perror("fopen");
        return false;
    }

    out.clear();
    out.reserve(4096);

    char line[8192];
    while (fgets(line, sizeof line, in)) {
        std::string source(line);
        std::string trimmed = trim_copy(source);
        size_t comment_pos = trimmed.find(';');
        if (comment_pos != std::string::npos) {
            trimmed.erase(comment_pos);
        }
        trimmed = trim_copy(trimmed);
        const char* s = trimmed.c_str();

        if (starts_with_ci(s, "%include")) {
            const char* p = s + 8;
            while (*p && isspace((unsigned char) *p)) {
                p++;
            }

            if (*p != '"') {
                fprintf(stderr, "Error: malformed %%include directive\n");
                fclose(in);
                return false;
            }

            const char* end = strchr(p + 1, '"');
            if (!end) {
                fprintf(stderr, "Error: malformed %%include directive\n");
                fclose(in);
                return false;
            }

            std::string include_target(p + 1, (size_t) (end - (p + 1)));

            std::string include_path;
            if (!base_dir.empty()) {
                include_path = base_dir + "/" + include_target;
            } else {
                include_path = include_target;
            }

            std::string included;
            if (!preprocess_includes_impl(include_path.c_str(), depth + 1, included)) {
                fclose(in);
                return false;
            }
            out += included;
            continue;
        }

        out += line;
    }

    fclose(in);
    return true;
}

bool preprocess_includes(const std::string& input, std::string& out) {
    return preprocess_includes_impl(input.c_str(), 0, out);
}

} // namespace tools
} // namespace blackbox
