#include "macro_expansion.hpp"
#include <cctype>
#include <format>

namespace blackbox::tools {
static std::string replace_all(std::string text, std::string_view needle,
                               std::string_view replacement) {
    if (needle.empty()) {
        return text;
    }
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

static std::vector<std::string> split_tokens(std::string_view s) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) {
            i++;
        }
        if (i >= s.size()) {
            break;
        }
        size_t start = i;
        while (i < s.size() && !isspace(static_cast<unsigned char>(s[i])) && s[i] != ',') {
            i++;
        }
        tokens.emplace_back(s.substr(start, i - start));
    }
    return tokens;
}

static std::string_view trim_sv(std::string_view s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

static const bbxc::asm_helpers::Macro*
find_macro(std::string_view name, const std::vector<bbxc::asm_helpers::Macro>& macros) {
    for (auto& m : macros) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}
bool expand_invocation(std::string_view line, const std::vector<bbxc::asm_helpers::Macro>& macros,
                       std::vector<std::string>& out, unsigned long& expand_id, int depth) {
    if (depth > 32) {
        return false;
    }

    std::string_view s = trim_sv(line);
    if (s.empty()) {
        return false;
    }

    if (s[0] == '%') {
        s.remove_prefix(1);
    } else {
        return false;
    }

    auto tokens = split_tokens(s);
    if (tokens.empty()) {
        return false;
    }

    const auto* m = find_macro(tokens[0], macros);
    if (!m) {
        return false;
    }

    std::vector<std::string> args(tokens.begin() + 1, tokens.end());
    expand_id++;

    std::string id_prefix = std::format("M{}", expand_id);

    for (auto& body_line : m->body) {
        std::string expanded = body_line;

        // replace named params: $paramname
        for (size_t pi = 0; pi < m->params.size(); pi++) {
            std::string needle = "$" + m->params[pi];
            std::string_view repl = (pi < args.size()) ? args[pi] : std::string_view{};
            expanded = replace_all(std::move(expanded), needle, repl);
        }

        // replace @@ label prefixes with unique id
        {
            std::string result;
            result.reserve(expanded.size() + 16);
            size_t pos = 0;
            while (pos < expanded.size()) {
                size_t at = expanded.find("@@", pos);
                if (at == std::string::npos) {
                    result.append(expanded, pos, std::string::npos);
                    break;
                }
                result.append(expanded, pos, at - pos);
                result += id_prefix;
                result += '_';
                pos = at + 2;
                while (
                    pos < expanded.size() &&
                    (isalnum(static_cast<unsigned char>(expanded[pos])) || expanded[pos] == '_')) {
                    result += expanded[pos];
                    pos++;
                }
            }
            expanded = std::move(result);
        }
        std::string_view wline = trim_sv(expanded);
        if (wline.empty()) {
            continue;
        }

        if (wline[0] == '%') {
            expand_invocation(wline, macros, out, expand_id, depth + 1);
        } else {
            out.emplace_back(wline);
        }
    }
    return true;
}
} // namespace blackbox::tools