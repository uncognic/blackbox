#include "asm_util.hpp"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../define.hpp"

namespace bbxc {
namespace asm_helpers {

std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && isspace((unsigned char) s[start])) {
        start++;
    }

    size_t end = s.size();
    while (end > start && isspace((unsigned char) s[end - 1])) {
        end--;
    }

    return s.substr(start, end - start);
}

void trim_crlf(char* s) {
    if (!s) {
        return;
    }

    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[len - 1] = '\0';
        len--;
    }
}

int failf(FILE* in, FILE* out, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);

    if (in) {
        fclose(in);
    }
    if (out) {
        fclose(out);
    }
    return 1;
}

void append_le_bytes(std::vector<uint8_t>& buf, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; i++) {
        buf.push_back((uint8_t) ((value >> (i * 8)) & 0xFF));
    }
}

void append_data_item(std::vector<Data>& data, const char* name, DataType type, uint32_t offset,
                      uint64_t value) {
    Data item = {};
    snprintf(item.name, sizeof(item.name), "%s", name);
    item.type = type;
    item.offset = offset;

    switch (type) {
        case DATA_BYTE:
            item.byte = (uint8_t) value;
            break;
        case DATA_WORD:
            item.word = (uint16_t) value;
            break;
        case DATA_DWORD:
            item.dword = (uint32_t) value;
            break;
        case DATA_QWORD:
            item.qword = value;
            break;
        case DATA_STRING:
            item.str = NULL;
            break;
    }

    data.push_back(item);
}

std::vector<std::string> split_tokens(const std::string& s) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (isspace((unsigned char) s[i]) || s[i] == ',')) {
            i++;
        }
        if (i >= s.size()) {
            break;
        }
        size_t start = i;
        while (i < s.size() && !isspace((unsigned char) s[i]) && s[i] != ',') {
            i++;
        }
        tokens.emplace_back(s.substr(start, i - start));
    }
    return tokens;
}

char* dup_cstr(const std::string& s) {
    char* p = static_cast<char*>(malloc(s.size() + 1));
    if (!p) {
        return NULL;
    }
    memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

void free_macro_owned(Macro& m) {
    free(m.name);
    for (int i = 0; i < m.paramc; i++) {
        free(m.params[i]);
    }
    free(m.params);
    for (int i = 0; i < m.bodyc; i++) {
        free(m.body[i]);
    }
    free(m.body);
    m = Macro{};
}

bool build_macro_owned(const std::string& name, const std::vector<std::string>& params,
                       const std::vector<std::string>& body, Macro& out) {
    out = Macro{};
    out.name = dup_cstr(name);
    if (!out.name) {
        return false;
    }

    out.paramc = (int) params.size();
    if (out.paramc > 0) {
        out.params = static_cast<char**>(calloc((size_t) out.paramc, sizeof(char*)));
        if (!out.params) {
            free_macro_owned(out);
            return false;
        }
        for (int i = 0; i < out.paramc; i++) {
            out.params[i] = dup_cstr(params[(size_t) i]);
            if (!out.params[i]) {
                free_macro_owned(out);
                return false;
            }
        }
    }

    out.bodyc = (int) body.size();
    if (out.bodyc > 0) {
        out.body = static_cast<char**>(calloc((size_t) out.bodyc, sizeof(char*)));
        if (!out.body) {
            free_macro_owned(out);
            return false;
        }
        for (int i = 0; i < out.bodyc; i++) {
            out.body[i] = dup_cstr(body[(size_t) i]);
            if (!out.body[i]) {
                free_macro_owned(out);
                return false;
            }
        }
    }

    return true;
}

int collect_lines_from_buffer(const char* src, std::vector<std::string>& lines) {
    const char* p = src;
    while (*p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t) (nl - p + 1) : strlen(p);
        lines.emplace_back(p, len);

        if (!nl) {
            break;
        }
        p = nl + 1;
    }

    return 0;
}

} // namespace asm_helpers
} // namespace bbxc
