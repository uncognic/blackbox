#include "macro_expansion.hpp"
#include "../define.hpp"
#include "string_utils.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>


namespace blackbox {
namespace tools {
static std::vector<std::string> split_tokens(const std::string& s) {
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

static std::string replace_all_cpp(std::string text, const std::string& needle,
                                   const std::string& replacement) {
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

Macro* find_macro(Macro* macros, size_t macro_count, const char* name) {
    const std::string needle(name ? name : "");
    for (size_t m = 0; m < macro_count; m++) {
        if (std::string(macros[m].name) == needle) {
            return &macros[m];
        }
    }
    return nullptr;
}

int expand_invocation(const char* invocation_line, FILE* dest, int depth, Macro* macros,
                      size_t macro_count, unsigned long* expand_id) {
    if (depth > 32) {
        return -1;
    }

    std::vector<char> inv_mut(invocation_line, invocation_line + strlen(invocation_line));
    inv_mut.push_back('\0');
    char* t = trim(inv_mut.data());
    if (t[0] != '%') {
        return 0;
    }

    std::vector<std::string> tokens = split_tokens(std::string(t + 1));
    if (tokens.empty()) {
        return 0;
    }

    Macro* m = find_macro(macros, macro_count, tokens[0].c_str());
    if (!m) {
        return 0;
    }

    std::vector<std::string> args;
    for (size_t i = 1; i < tokens.size() && args.size() < 32; i++) {
        args.push_back(tokens[i]);
    }

    (*expand_id)++;
    std::string id_prefix = "M" + std::to_string(*expand_id);

    for (int bi = 0; bi < m->bodyc; bi++) {
        std::string line = m->body[bi];
        for (int pi = 0; pi < m->paramc; pi++) {
            std::string find = "$" + std::string(m->params[pi]);
            const std::string repl = (pi < static_cast<int>(args.size())) ? args[static_cast<size_t>(pi)] : std::string();
            line = replace_all_cpp(std::move(line), find, repl);
        }
        for (size_t pi = 0; pi < args.size(); pi++) {
            std::string find = "$" + std::to_string(pi + 1);
            line = replace_all_cpp(std::move(line), find, args[pi]);
        }

        const char* pcur = line.c_str();
        std::string out;
        out.reserve(line.size() + 16);
        for (;;) {
            const char* atpos = strstr(pcur, "@@");
            if (!atpos) {
                out += pcur;
                break;
            }
            size_t prefixlen = static_cast<size_t>(atpos - pcur);
            out.append(pcur, prefixlen);
            const char* ident = atpos + 2;
            int il = 0;
            while (ident[il] && (isalnum(static_cast<unsigned char>(ident[il])) || ident[il] == '_')) {
                il++;
            }
            out += id_prefix;
            out += "_";
            out.append(ident, static_cast<size_t>(il));
            pcur = ident + il;
        }

        std::vector<char> out_mut(out.begin(), out.end());
        out_mut.push_back('\0');
        char* wline = trim(out_mut.data());
        if (wline[0] == '%') {
            expand_invocation(wline, dest, depth + 1, macros, macro_count, expand_id);
        } else {
            fputs(wline, dest);
            size_t l = strlen(wline);
            if (l == 0 || wline[l - 1] != '\n') {
                fputc('\n', dest);
            }
        }
    }

    return 1;
}

} // namespace tools
} // namespace blackbox
