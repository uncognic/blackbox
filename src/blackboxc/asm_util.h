#ifndef ASM_UTIL_H
#define ASM_UTIL_H

#include "../data.h"
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace bbxc {
namespace asm_helpers {

std::string trim_copy(const std::string& s);
void trim_crlf(char* s);
int failf(FILE* in, FILE* out, const char* fmt, ...);

void append_le_bytes(std::vector<uint8_t>& buf, uint64_t value, size_t width);
void append_data_item(std::vector<Data>& data, const char* name, DataType type, uint32_t offset,
                      uint64_t value);
std::vector<std::string> split_tokens(const std::string& s);
char* dup_cstr(const std::string& s);
void free_macro_owned(Macro& m);
bool build_macro_owned(const std::string& name, const std::vector<std::string>& params,
                       const std::vector<std::string>& body, Macro& out);
int collect_lines_from_buffer(const char* src, std::vector<std::string>& lines);

template <typename... Args> void dbg(bool debug, const char* fmt, const Args&... args) {
    if (debug) {
        printf(fmt, args...);
    }
}

} // namespace asm_helpers
} // namespace bbxc

#endif
