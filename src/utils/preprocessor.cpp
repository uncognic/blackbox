#include "preprocessor.hpp"
#include "string_utils.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <print>
#include <string>
#include <vector>

namespace blackbox {
namespace tools {

static std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }

    size_t end = s.size();
    while (end > start &&
           (isspace(static_cast<unsigned char>(s[end - 1])) || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        end--;
    }

    return s.substr(start, end - start);
}

static bool preprocess_includes_impl(const char* input, int depth, std::string& out) {
    if (depth > 32) {
        std::println(stderr, "Error: include nesting too deep");
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

        if (starts_with_ci(s, ".include")) {
            const char* p = s + 8;
            while (*p && isspace(static_cast<unsigned char>(*p))) {
                p++;
            }

            if (*p != '"') {
                std::println(stderr, "Error: malformed .include directive");
                fclose(in);
                return false;
            }

            const char* end = strchr(p + 1, '"');
            if (!end) {
                std::println(stderr, "Error: malformed .include directive");
                fclose(in);
                return false;
            }

            std::string include_target(p + 1, static_cast<size_t>(end - (p + 1)));

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

bool preprocess_defines(std::string& input, std::unordered_map<std::string, std::string>& defines) {
    std::vector<std::string> lines;
    size_t pos = 0;

    while (pos < input.size()) {
        size_t line_end = input.find('\n', pos);
        if (line_end == std::string::npos) {
            line_end = input.size();
        }

        std::string line = input.substr(pos, line_end - pos);
        std::string trimmed = trim_copy(line);

        size_t comment_pos = trimmed.find(';');
        if (comment_pos != std::string::npos) {
            trimmed.erase(comment_pos);
            trimmed = trim_copy(trimmed);
        }

        if (starts_with_ci(trimmed.data(), ".define")) {
            const char* p = trimmed.c_str() + 7; // skip ".define"
            while (*p && isspace(static_cast<unsigned char>(*p))) {
                p++;
            }

            const char* sym_start = p;
            while (*p && !isspace(static_cast<unsigned char>(*p))) {
                p++;
            }

            if (sym_start == p) {
                std::println(stderr, "Error: malformed .define directive (missing symbol)");
                return false;
            }

            std::string symbol(sym_start, static_cast<size_t>(p - sym_start));

            while (*p && isspace(static_cast<unsigned char>(*p))) {
                p++;
            }

            std::string value = trim_copy(std::string(p));

            defines[symbol] = value;

            pos = line_end + 1;
            continue;
        }

        lines.push_back(line);
        pos = line_end + 1;
    }

    input.clear();
    for (size_t i = 0; i < lines.size(); i++) {
        std::string line = lines[i];

        for (const auto& [symbol, value] : defines) {
            size_t search_pos = 0;
            while ((search_pos = line.find(symbol, search_pos)) != std::string::npos) {
                bool valid_before = (search_pos == 0 || !isalnum(static_cast<unsigned char>(line[search_pos - 1])));
                bool valid_after = (search_pos + symbol.size() >= line.size() ||
                                  !isalnum(static_cast<unsigned char>(line[search_pos + symbol.size()])));

                if (valid_before && valid_after) {
                    line.replace(search_pos, symbol.size(), value);
                    search_pos += value.size();
                } else {
                    search_pos += symbol.size();
                }
            }
        }

        input += line;
        if (i < lines.size() - 1 || pos <= input.size()) {
            input += '\n';
        }
    }

    return true;
}

} // namespace tools
} // namespace blackbox
