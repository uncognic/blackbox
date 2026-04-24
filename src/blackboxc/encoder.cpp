//
// Created by User on 2026-04-19.
//

#include "encoder.hpp"
#include "../define.hpp"
#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <print>
#include <string>


namespace bbxc::encoder {

// writers
void write_u8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

void write_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
}

void write_u32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >> 24));
}

void write_u64(std::vector<uint8_t>& buf, uint64_t v) {
    write_u32(buf, static_cast<uint32_t>(v));
    write_u32(buf, static_cast<uint32_t>(v >> 32));
}

void write_i32(std::vector<uint8_t>& buf, int32_t v) {
    write_u32(buf, static_cast<uint32_t>(v));
}

void write_i64(std::vector<uint8_t>& buf, int64_t v) {
    write_u64(buf, static_cast<uint64_t>(v));
}

std::optional<uint8_t> parse_register(std::string_view name) {
    while (!name.empty() && isspace(static_cast<unsigned char>(name.front()))) {
        name.remove_prefix(1);
    }
    while (!name.empty() && isspace(static_cast<unsigned char>(name.back()))) {
        name.remove_suffix(1);
    }

    if (name.size() < 2) {
        return std::nullopt;
    }
    if (name[0] != 'R' && name[0] != 'r') {
        return std::nullopt;
    }

    uint8_t idx = 0;
    auto result = std::from_chars(name.data() + 1, name.data() + name.size(), idx);
    if (result.ec != std::errc{} || result.ptr != name.data() + name.size()) {
        return std::nullopt;
    }
    if (idx >= REGISTERS) {
        return std::nullopt;
    }

    return idx;
}
std::optional<uint8_t> parse_file_descriptor(std::string_view name) {
    while (!name.empty() && isspace(static_cast<unsigned char>(name.front()))) {
        name.remove_prefix(1);
    }
    while (!name.empty() && isspace(static_cast<unsigned char>(name.back()))) {
        name.remove_suffix(1);
    }

    if (name == "STDOUT" || name == "stdout") {
        return 1;
    }
    if (name == "STDERR" || name == "stderr") {
        return 2;
    }
    if (name == "STDIN" || name == "stdin") {
        return 0;
    }

    if (name.size() < 2) {
        return std::nullopt;
    }
    if (name[0] != 'F' && name[0] != 'f') {
        return std::nullopt;
    }

    uint8_t idx = 0;
    auto result = std::from_chars(name.data() + 1, name.data() + name.size(), idx);
    if (result.ec != std::errc{} || result.ptr != name.data() + name.size()) {
        return std::nullopt;
    }
    if (idx >= FILE_DESCRIPTORS) {
        return std::nullopt;
    }

    return idx;
}

static std::string_view trim(std::string_view s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

static std::string_view strip_comment(std::string_view s) {
    size_t pos = s.find(';');
    if (pos != std::string_view::npos) {
        s = s.substr(0, pos);
    }
    return trim(s);
}
static bool starts_with_keyword(std::string_view s, std::string_view kw) {
    if (s.size() < kw.size()) {
        return false;
    }
    for (size_t i = 0; i < kw.size(); i++) {
        if (tolower(static_cast<unsigned char>(s[i])) !=
            tolower(static_cast<unsigned char>(kw[i]))) {
            return false;
        }
    }
    if (s.size() == kw.size()) {
        return true;
    }
    return isspace(static_cast<unsigned char>(s[kw.size()]));
}

static std::string_view after_keyword(std::string_view s, size_t kw_len) {
    s.remove_prefix(kw_len);
    return trim(s);
}

// parse quoted string and move pos
static std::optional<std::string_view> parse_quoted(std::string_view s, size_t& pos) {
    if (pos >= s.size() || s[pos] != '"') {
        return std::nullopt;
    }
    size_t start = pos + 1;
    size_t end = s.find('"', start);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    pos = end + 1;
    return s.substr(start, end - start);
}

static std::optional<int32_t> parse_i32(std::string_view s) {
    s = trim(s);
    int32_t v = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

static std::optional<uint32_t> parse_u32(std::string_view s) {
    s = trim(s);
    uint32_t v = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

static std::optional<int64_t> parse_i64(std::string_view s) {
    s = trim(s);
    int64_t v = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}
// split on first comma, returning lhs and rhs trimmed
static std::pair<std::string_view, std::string_view> split_comma(std::string_view s) {
    size_t pos = s.find(',');
    if (pos == std::string_view::npos) {
        return {trim(s), {}};
    }
    return {trim(s.substr(0, pos)), trim(s.substr(pos + 1))};
}

// find label addr by name
static std::optional<uint32_t> resolve_label(std::string_view name,
                                             const std::vector<asm_helpers::Label>& labels) {
    name = trim(name);
    for (auto& l : labels) {
        if (l.name == name) {
            return l.addr;
        }
    }
    return std::nullopt;
}

// find data index by name
static std::optional<uint32_t> resolve_data(std::string_view name,
                                            const std::vector<asm_helpers::DataEntry>& entries) {
    name = trim(name);
    if (!name.empty() && name[0] == '$') {
        name.remove_prefix(1);
    }
    for (auto& e : entries) {
        if (e.name == name) {
            return e.index;
        }
    }
    return std::nullopt;
}

// for variable size instructions
static size_t quoted_string_len(std::string_view s) {
    size_t q = s.find('"');
    if (q == std::string_view::npos) {
        return 0;
    }
    size_t end = s.find('"', q + 1);
    if (end == std::string_view::npos) {
        return 0;
    }
    return end - q - 1;
}
size_t instr_size(std::string_view line) {
    std::string_view s = strip_comment(line);
    if (s.empty()) {
        return 0;
    }
    if (s[0] == '.') {
        return 0; // label definition
    }
    if (s[0] == '%') {
        return 0; // assembler directive
    }

    // variable-length instructions
    if (starts_with_keyword(s, "WRITE")) {
        return 6 + quoted_string_len(s);
    }
    if (starts_with_keyword(s, "FOPEN")) {
        return 7 + quoted_string_len(s);
    }
    if (starts_with_keyword(s, "EXEC")) {
        return 6 + quoted_string_len(s);
    }
    if (starts_with_keyword(s, "GETENV")) {
        auto rest = trim(after_keyword(s, 6));
        auto [reg_part, name_part] = split_comma(rest);
        name_part = trim(name_part);
        if (!name_part.empty() && name_part[0] == '"') {
            return 6 + quoted_string_len(s);
        }
        return 6 + name_part.size();
    }

    // fixed-size instructions
    if (starts_with_keyword(s, "HALT")) {
        return 2;
    }
    if (starts_with_keyword(s, "NEWLINE")) {
        return 1;
    }
    if (starts_with_keyword(s, "CLRSCR")) {
        return 1;
    }
    if (starts_with_keyword(s, "RET")) {
        return 1;
    }
    if (starts_with_keyword(s, "SYSRET")) {
        return 1;
    }
    if (starts_with_keyword(s, "FAULTRET")) {
        return 1;
    }
    if (starts_with_keyword(s, "DROPPRIV")) {
        return 1;
    }
    if (starts_with_keyword(s, "DUMPREGS")) {
        return 1;
    }
    if (starts_with_keyword(s, "BREAK")) {
        return 1;
    }
    if (starts_with_keyword(s, "CONTINUE")) {
        return 1;
    }
    if (starts_with_keyword(s, "PRINT_STACKSIZE")) {
        return 1;
    }

    if (starts_with_keyword(s, "PRINT")) {
        return 2;
    }
    if (starts_with_keyword(s, "INC")) {
        return 2;
    }
    if (starts_with_keyword(s, "DEC")) {
        return 2;
    }
    if (starts_with_keyword(s, "PUSH")) {
        auto operand = trim(after_keyword(s, 4));
        if (operand.empty()) {
            return 0;
        }

        return parse_register(operand).has_value() ? 2 : 5;
    }
    if (starts_with_keyword(s, "POP")) {
        return 2;
    }
    if (starts_with_keyword(s, "NOT")) {
        return 2;
    }
    if (starts_with_keyword(s, "PRINTREG")) {
        return 2;
    }
    if (starts_with_keyword(s, "EPRINTREG")) {
        return 2;
    }
    if (starts_with_keyword(s, "PRINTSTR")) {
        return 2;
    }
    if (starts_with_keyword(s, "EPRINTSTR")) {
        return 2;
    }
    if (starts_with_keyword(s, "PRINTCHAR")) {
        return 2;
    }
    if (starts_with_keyword(s, "EPRINTCHAR")) {
        return 2;
    }
    if (starts_with_keyword(s, "READSTR")) {
        return 2;
    }
    if (starts_with_keyword(s, "READCHAR")) {
        return 2;
    }
    if (starts_with_keyword(s, "READ")) {
        return 2;
    }
    if (starts_with_keyword(s, "GETKEY")) {
        return 2;
    }
    if (starts_with_keyword(s, "GETARGC")) {
        return 2;
    }
    if (starts_with_keyword(s, "FCLOSE")) {
        return 2;
    }
    if (starts_with_keyword(s, "GETMODE")) {
        return 2;
    }
    if (starts_with_keyword(s, "GETFAULT")) {
        return 2;
    }
    if (starts_with_keyword(s, "SYSCALL")) {
        return 2;
    }

    if (starts_with_keyword(s, "ADD")) {
        return 3;
    }
    if (starts_with_keyword(s, "SUB")) {
        return 3;
    }
    if (starts_with_keyword(s, "MUL")) {
        return 3;
    }
    if (starts_with_keyword(s, "DIV")) {
        return 3;
    }
    if (starts_with_keyword(s, "MOD")) {
        return 3;
    }
    if (starts_with_keyword(s, "MOV")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));
        return parse_register(src_tok).has_value() ? 3 : 6;
    }
    if (starts_with_keyword(s, "CMP")) {
        return 3;
    }
    if (starts_with_keyword(s, "AND")) {
        return 3;
    }
    if (starts_with_keyword(s, "OR")) {
        return 3;
    }
    if (starts_with_keyword(s, "XOR")) {
        return 3;
    }
    if (starts_with_keyword(s, "SHL")) {
        return 3;
    }
    if (starts_with_keyword(s, "SHR")) {
        return 3;
    }
    if (starts_with_keyword(s, "LOAD_REG")) {
        return 3;
    }
    if (starts_with_keyword(s, "STORE_REG")) {
        return 3;
    }
    if (starts_with_keyword(s, "LOADVAR_REG")) {
        return 3;
    }
    if (starts_with_keyword(s, "STOREVAR_REG")) {
        return 3;
    }
    if (starts_with_keyword(s, "FREAD")) {
        return 3;
    }
    if (starts_with_keyword(s, "FWRITE_REG")) {
        return 3;
    }
    if (starts_with_keyword(s, "FWRITE")) {
        auto [fd_tok, value_tok] = split_comma(after_keyword(s, 6));
        (void) fd_tok;
        return parse_register(value_tok).has_value() ? 3 : 6;
    }
    if (starts_with_keyword(s, "FSEEK_REG")) {
        return 3;
    }
    if (starts_with_keyword(s, "FSEEK")) {
        auto [fd_tok, value_tok] = split_comma(after_keyword(s, 5));
        (void) fd_tok;
        return parse_register(value_tok).has_value() ? 3 : 6;
    }
    if (starts_with_keyword(s, "LOADREF")) {
        return 3;
    }
    if (starts_with_keyword(s, "STOREREF")) {
        return 3;
    }

    if (starts_with_keyword(s, "PUSHI")) {
        return 5;
    }
    if (starts_with_keyword(s, "JE")) {
        return 5;
    }
    if (starts_with_keyword(s, "JNE")) {
        return 5;
    }
    if (starts_with_keyword(s, "JL")) {
        return 5;
    }
    if (starts_with_keyword(s, "JGE")) {
        return 5;
    }
    if (starts_with_keyword(s, "JB")) {
        return 5;
    }
    if (starts_with_keyword(s, "JAE")) {
        return 5;
    }
    if (starts_with_keyword(s, "SLEEP")) {
        auto operand = trim(after_keyword(s, 5));
        return parse_register(operand).has_value() ? 2 : 5;
    }
    if (starts_with_keyword(s, "ALLOC")) {
        return 5;
    }
    if (starts_with_keyword(s, "GROW")) {
        return 5;
    }
    if (starts_with_keyword(s, "RESIZE")) {
        return 5;
    }
    if (starts_with_keyword(s, "FREE")) {
        return 5;
    }

    if (starts_with_keyword(s, "JMP")) {
        auto operand = after_keyword(s, 3);
        return parse_register(operand).has_value() ? 2 : 5;
    }
    if (starts_with_keyword(s, "LOAD")) {
        auto [reg_tok, value_tok] = split_comma(after_keyword(s, 4));
        static_cast<void>(reg_tok);
        return parse_register(value_tok).has_value() ? 3 : 6;
    }
    if (starts_with_keyword(s, "STORE")) {
        auto [reg_tok, value_tok] = split_comma(after_keyword(s, 5));
        static_cast<void>(reg_tok);
        return parse_register(value_tok).has_value() ? 3 : 6;
    }
    if (starts_with_keyword(s, "LOADVAR")) {
        return 6;
    }
    if (starts_with_keyword(s, "STOREVAR")) {
        return 6;
    }
    if (starts_with_keyword(s, "LOADGLOBAL")) {
        return 6;
    }
    if (starts_with_keyword(s, "STOREGLOBAL")) {
        return 6;
    }
    if (starts_with_keyword(s, "LOADSTR")) {
        return 6;
    }
    if (starts_with_keyword(s, "FWRITE_IMM")) {
        return 6;
    }
    if (starts_with_keyword(s, "FSEEK_IMM")) {
        return 6;
    }
    if (starts_with_keyword(s, "GETARG")) {
        return 6;
    }
    if (starts_with_keyword(s, "REGSYSCALL")) {
        return 6;
    }
    if (starts_with_keyword(s, "REGFAULT")) {
        return 6;
    }

    if (starts_with_keyword(s, "CALL")) {
        return 9;
    }
    if (starts_with_keyword(s, "SHLI")) {
        return 10;
    }
    if (starts_with_keyword(s, "SHRI")) {
        return 10;
    }
    if (starts_with_keyword(s, "RAND")) {
        return 18;
    }
    if (starts_with_keyword(s, "SETPERM")) {
        return 13;
    }

    return 0;
}

std::expected<void, std::string> encode(std::string_view line,
                                        const std::vector<asm_helpers::Label>& labels,
                                        const std::vector<asm_helpers::DataEntry>& data_entries,
                                        std::vector<uint8_t>& out, bool debug) {
    std::string_view s = strip_comment(line);
    if (s.empty() || s[0] == '.' || s[0] == '%') {
        return {};
    }

    auto err = [&](std::string msg) -> std::expected<void, std::string> {
        return std::unexpected(std::format("{}: '{}'", msg, s));
    };

    auto need_reg = [&](std::string_view tok) -> std::expected<uint8_t, std::string> {
        auto r = parse_register(tok);
        if (!r) {
            return std::unexpected(std::format("invalid register '{}' in '{}'", tok, s));
        }
        return *r;
    };

    auto need_fd = [&](std::string_view tok) -> std::expected<uint8_t, std::string> {
        auto f = parse_file_descriptor(tok);
        if (!f) {
            return std::unexpected(std::format("invalid file descriptor '{}' in '{}'", tok, s));
        }
        return *f;
    };

    auto need_label = [&](std::string_view name) -> std::expected<uint32_t, std::string> {
        auto addr = resolve_label(name, labels);
        if (!addr) {
            return std::unexpected(std::format("undefined label '{}' in '{}'", name, s));
        }
        return *addr;
    };

    auto need_data = [&](std::string_view name) -> std::expected<uint32_t, std::string> {
        auto idx = resolve_data(name, data_entries);
        if (!idx) {
            return std::unexpected(std::format("undefined data entry '{}' in '{}'", name, s));
        }
        return *idx;
    };

    // helper macros to propagate errors cleanly
#define TRY_REG(var, tok)                                                                          \
    auto var##_r = need_reg(tok);                                                                  \
    if (!var##_r)                                                                                  \
        return std::unexpected(var##_r.error());                                                   \
    uint8_t var = *var##_r;

#define TRY_FD(var, tok)                                                                           \
    auto var##_r = need_fd(tok);                                                                   \
    if (!var##_r)                                                                                  \
        return std::unexpected(var##_r.error());                                                   \
    uint8_t var = *var##_r;

#define TRY_LABEL(var, tok)                                                                        \
    auto var##_r = need_label(tok);                                                                \
    if (!var##_r)                                                                                  \
        return std::unexpected(var##_r.error());                                                   \
    uint32_t var = *var##_r;

#define TRY_DATA(var, tok)                                                                         \
    auto var##_r = need_data(tok);                                                                 \
    if (!var##_r)                                                                                  \
        return std::unexpected(var##_r.error());                                                   \
    uint32_t var = *var##_r;

    if (debug) {
        std::println("[ENC] {}", s);
    }

    if (starts_with_keyword(s, "ADD")) {
        auto [dst, src] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::ADD));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "SUB")) {
        auto [dst, src] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::SUB));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "MUL")) {
        auto [dst, src] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::MUL));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "DIV")) {
        auto [dst, src] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::DIV));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "MOD")) {
        auto [dst, src] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::MOD));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "INC")) {
        TRY_REG(r, after_keyword(s, 3))
        write_u8(out, opcode_to_byte(Opcode::INC));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "DEC")) {
        TRY_REG(r, after_keyword(s, 3))
        write_u8(out, opcode_to_byte(Opcode::DEC));
        write_u8(out, r);
        return {};
    }

    if (starts_with_keyword(s, "AND")) {
        auto [dst, src] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::AND));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "OR")) {
        auto [dst, src] = split_comma(after_keyword(s, 2));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::OR));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "XOR")) {
        auto [dst, src] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::XOR));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "NOT")) {
        TRY_REG(r, after_keyword(s, 3))
        write_u8(out, opcode_to_byte(Opcode::NOT));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "SHLI")) {
        auto [reg_tok, shift_tok] = split_comma(after_keyword(s, 4));
        TRY_REG(r, reg_tok)
        auto shift = parse_u32(shift_tok);
        if (!shift) {
            return err("invalid shift amount in SHLI");
        }
        write_u8(out, opcode_to_byte(Opcode::SHLI));
        write_u8(out, r);
        write_u64(out, static_cast<uint64_t>(*shift));
        return {};
    }
    if (starts_with_keyword(s, "SHRI")) {
        auto [reg_tok, shift_tok] = split_comma(after_keyword(s, 4));
        TRY_REG(r, reg_tok)
        auto shift = parse_u32(shift_tok);
        if (!shift) {
            return err("invalid shift amount in SHRI");
        }
        write_u8(out, opcode_to_byte(Opcode::SHRI));
        write_u8(out, r);
        write_u64(out, static_cast<uint64_t>(*shift));
        return {};
    }
    if (starts_with_keyword(s, "SHL")) {
        auto [dst, src] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::SHL));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "SHR")) {
        auto [dst, src] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::SHR));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }

    if (starts_with_keyword(s, "MOV")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst_tok); // dst can only be a reg

        if (auto reg = parse_register(src_tok)) {
            write_u8(out, opcode_to_byte(Opcode::MOV_REG));
            write_u8(out, d);
            write_u8(out, *reg);
            return {};
        }

        if (auto imm = parse_i32(src_tok)) {
            write_u8(out, opcode_to_byte(Opcode::MOVI));
            write_u8(out, d);
            write_i32(out, *imm);
            return {};
        }

        return err("invalid source operand in MOV (expected register or i32 immediate)");
    }
    if (starts_with_keyword(s, "PUSH")) {
        auto arg = trim(after_keyword(s, 4));
        if (arg.empty()) {
            return err("missing operand in PUSH");
        }

        // if is reg
        if (auto reg = parse_register(arg)) {
            write_u8(out, opcode_to_byte(Opcode::PUSH_REG));
            write_u8(out, *reg);
            return {};
        }

        // if imm
        if (auto imm = parse_i32(arg)) {
            write_u8(out, opcode_to_byte(Opcode::PUSHI));
            write_i32(out, *imm);
            return {};
        }

        return err("invalid operand in PUSH (expected register or i32 immediate)");
    }
    if (starts_with_keyword(s, "POP")) {
        TRY_REG(r, after_keyword(s, 3))
        write_u8(out, opcode_to_byte(Opcode::POP));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "CMP")) {
        auto [a, b] = split_comma(after_keyword(s, 3));
        TRY_REG(ra, a) TRY_REG(rb, b) write_u8(out, opcode_to_byte(Opcode::CMP));
        write_u8(out, ra);
        write_u8(out, rb);
        return {};
    }

    if (starts_with_keyword(s, "JMP")) {
        auto target = trim(after_keyword(s, 3));
        if (target.empty()) {
            return err("missing JMP operand");
        }

        if (auto reg = parse_register(target)) {
            write_u8(out, opcode_to_byte(Opcode::JMP));
            write_u8(out, *reg);
            return {};
        }

        if (auto addr = resolve_label(target, labels)) {
            write_u8(out, opcode_to_byte(Opcode::JMPI));
            write_u32(out, *addr);
            return {};
        }

        if (auto imm = parse_u32(target)) {
            write_u8(out, opcode_to_byte(Opcode::JMPI));
            write_u32(out, *imm);
            return {};
        }

        return err("invalid JMP operand (expected register, label, or u32 immediate)");
    }
    if (starts_with_keyword(s, "JE")) {
        TRY_LABEL(addr, after_keyword(s, 2))
        write_u8(out, opcode_to_byte(Opcode::JE));
        write_u32(out, addr);
        return {};
    }
    if (starts_with_keyword(s, "JNE")) {
        TRY_LABEL(addr, after_keyword(s, 3))
        write_u8(out, opcode_to_byte(Opcode::JNE));
        write_u32(out, addr);
        return {};
    }
    if (starts_with_keyword(s, "JL")) {
        TRY_LABEL(addr, after_keyword(s, 2))
        write_u8(out, opcode_to_byte(Opcode::JL));
        write_u32(out, addr);
        return {};
    }
    if (starts_with_keyword(s, "JGE")) {
        TRY_LABEL(addr, after_keyword(s, 3))
        write_u8(out, opcode_to_byte(Opcode::JGE));
        write_u32(out, addr);
        return {};
    }
    if (starts_with_keyword(s, "JB")) {
        TRY_LABEL(addr, after_keyword(s, 2))
        write_u8(out, opcode_to_byte(Opcode::JB));
        write_u32(out, addr);
        return {};
    }
    if (starts_with_keyword(s, "JAE")) {
        TRY_LABEL(addr, after_keyword(s, 3))
        write_u8(out, opcode_to_byte(Opcode::JAE));
        write_u32(out, addr);
        return {};
    }
    if (starts_with_keyword(s, "CALL")) {
        auto [label_tok, frame_tok] = split_comma(after_keyword(s, 4));
        TRY_LABEL(addr, label_tok)
        // frame size: explicit override or from label
        uint32_t frame_size = 0;
        if (!frame_tok.empty()) {
            auto fs = parse_u32(frame_tok);
            if (!fs) {
                return err("invalid frame size in CALL");
            }
            frame_size = *fs;
        } else {
            for (auto& l : labels) {
                if (l.addr == addr) {
                    frame_size = l.frame_size;
                    break;
                }
            }
        }
        write_u8(out, opcode_to_byte(Opcode::CALL));
        write_u32(out, addr);
        write_u32(out, frame_size);
        return {};
    }
    if (starts_with_keyword(s, "RET")) {
        write_u8(out, opcode_to_byte(Opcode::RET));
        return {};
    }
    if (starts_with_keyword(s, "HALT")) {
        auto tok = trim(after_keyword(s, 4));
        uint8_t code = 0;
        if (!tok.empty()) {
            if (tok == "OK" || tok == "ok") {
                code = 0;
            } else if (tok == "BAD" || tok == "bad") {
                code = 1;
            } else {
                auto v = parse_u32(tok);
                if (!v) {
                    return err("invalid HALT operand");
                }
                code = static_cast<uint8_t>(*v);
            }
        }
        write_u8(out, opcode_to_byte(Opcode::HALT));
        write_u8(out, code);
        return {};
    }
    if (starts_with_keyword(s, "LOADVAR_REG")) {
        auto [reg_tok, idx_tok] = split_comma(after_keyword(s, 11));
        TRY_REG(r, reg_tok) TRY_REG(i, idx_tok) write_u8(out, opcode_to_byte(Opcode::LOADVAR_REG));
        write_u8(out, r);
        write_u8(out, i);
        return {};
    }
    if (starts_with_keyword(s, "STOREVAR_REG")) {
        auto [reg_tok, idx_tok] = split_comma(after_keyword(s, 12));
        TRY_REG(r, reg_tok) TRY_REG(i, idx_tok) write_u8(out, opcode_to_byte(Opcode::STOREVAR_REG));
        write_u8(out, r);
        write_u8(out, i);
        return {};
    }
    if (starts_with_keyword(s, "LOADVAR")) {
        auto [reg_tok, slot_tok] = split_comma(after_keyword(s, 7));
        TRY_REG(r, reg_tok)
        auto slot = parse_u32(slot_tok);
        if (!slot) {
            return err("invalid slot in LOADVAR");
        }
        write_u8(out, opcode_to_byte(Opcode::LOADVAR));
        write_u8(out, r);
        write_u32(out, *slot);
        return {};
    }
    if (starts_with_keyword(s, "STOREVAR")) {
        auto [reg_tok, slot_tok] = split_comma(after_keyword(s, 8));
        TRY_REG(r, reg_tok)
        auto slot = parse_u32(slot_tok);
        if (!slot) {
            return err("invalid slot in STOREVAR");
        }
        write_u8(out, opcode_to_byte(Opcode::STOREVAR));
        write_u8(out, r);
        write_u32(out, *slot);
        return {};
    }
    if (starts_with_keyword(s, "LOADGLOBAL")) {
        auto [reg_tok, slot_tok] = split_comma(after_keyword(s, 10));
        TRY_REG(r, reg_tok)
        auto slot = parse_u32(slot_tok);
        if (!slot) {
            return err("invalid slot in LOADGLOBAL");
        }
        write_u8(out, opcode_to_byte(Opcode::LOADGLOBAL));
        write_u8(out, r);
        write_u32(out, *slot);
        return {};
    }
    if (starts_with_keyword(s, "STOREGLOBAL")) {
        auto [reg_tok, slot_tok] = split_comma(after_keyword(s, 11));
        TRY_REG(r, reg_tok)
        auto slot = parse_u32(slot_tok);
        if (!slot) {
            return err("invalid slot in STOREGLOBAL");
        }
        write_u8(out, opcode_to_byte(Opcode::STOREGLOBAL));
        write_u8(out, r);
        write_u32(out, *slot);
        return {};
    }
    if (starts_with_keyword(s, "LOAD_REG")) {
        auto [reg_tok, idx_tok] = split_comma(after_keyword(s, 8));
        TRY_REG(r, reg_tok) TRY_REG(i, idx_tok) write_u8(out, opcode_to_byte(Opcode::LOAD_REG));
        write_u8(out, r);
        write_u8(out, i);
        return {};
    }
    if (starts_with_keyword(s, "STORE_REG")) {
        auto [reg_tok, idx_tok] = split_comma(after_keyword(s, 9));
        TRY_REG(r, reg_tok) TRY_REG(i, idx_tok) write_u8(out, opcode_to_byte(Opcode::STORE_REG));
        write_u8(out, r);
        write_u8(out, i);
        return {};
    }
    if (starts_with_keyword(s, "LOAD")) {
        auto [reg_tok, addr_tok] = split_comma(after_keyword(s, 4));
        TRY_REG(r, reg_tok)
        if (auto idx = parse_register(addr_tok)) {
            write_u8(out, opcode_to_byte(Opcode::LOAD_REG));
            write_u8(out, r);
            write_u8(out, *idx);
            return {};
        }
        auto addr = parse_u32(addr_tok);
        if (!addr) {
            return err("invalid address in LOAD");
        }
        write_u8(out, opcode_to_byte(Opcode::LOAD));
        write_u8(out, r);
        write_u32(out, *addr);
        return {};
    }
    if (starts_with_keyword(s, "STORE")) {
        auto [reg_tok, addr_tok] = split_comma(after_keyword(s, 5));
        TRY_REG(r, reg_tok)
        if (auto idx = parse_register(addr_tok)) {
            write_u8(out, opcode_to_byte(Opcode::STORE_REG));
            write_u8(out, r);
            write_u8(out, *idx);
            return {};
        }
        auto addr = parse_u32(addr_tok);
        if (!addr) {
            return err("invalid address in STORE");
        }
        write_u8(out, opcode_to_byte(Opcode::STORE));
        write_u8(out, r);
        write_u32(out, *addr);
        return {};
    }
    if (starts_with_keyword(s, "LOADREF")) {
        auto [dst, src] = split_comma(after_keyword(s, 7));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::LOADREF));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "STOREREF")) {
        auto [dst, src] = split_comma(after_keyword(s, 8));
        TRY_REG(d, dst) TRY_REG(r, src) write_u8(out, opcode_to_byte(Opcode::STOREREF));
        write_u8(out, d);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "ALLOC")) {
        auto v = parse_u32(after_keyword(s, 5));
        if (!v) {
            return err("invalid operand in ALLOC");
        }
        write_u8(out, opcode_to_byte(Opcode::ALLOC));
        write_u32(out, *v);
        return {};
    }
    if (starts_with_keyword(s, "GROW")) {
        auto v = parse_u32(after_keyword(s, 4));
        if (!v) {
            return err("invalid operand in GROW");
        }
        write_u8(out, opcode_to_byte(Opcode::GROW));
        write_u32(out, *v);
        return {};
    }
    if (starts_with_keyword(s, "RESIZE")) {
        auto v = parse_u32(after_keyword(s, 6));
        if (!v) {
            return err("invalid operand in RESIZE");
        }
        write_u8(out, opcode_to_byte(Opcode::RESIZE));
        write_u32(out, *v);
        return {};
    }
    if (starts_with_keyword(s, "FREE")) {
        auto v = parse_u32(after_keyword(s, 4));
        if (!v) {
            return err("invalid operand in FREE");
        }
        write_u8(out, opcode_to_byte(Opcode::FREE));
        write_u32(out, *v);
        return {};
    }
    if (starts_with_keyword(s, "LOADSTR")) {
        auto [name_tok, reg_tok] = split_comma(after_keyword(s, 7));
        TRY_DATA(idx, name_tok)
        TRY_REG(r, reg_tok)
        write_u8(out, opcode_to_byte(Opcode::LOADSTR));
        write_u8(out, r);
        write_u32(out, idx);
        return {};
    }
    if (starts_with_keyword(s, "PRINTSTR")) {
        TRY_REG(r, after_keyword(s, 8))
        write_u8(out, opcode_to_byte(Opcode::PRINTSTR));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "EPRINTSTR")) {
        TRY_REG(r, after_keyword(s, 9))
        write_u8(out, opcode_to_byte(Opcode::EPRINTSTR));
        write_u8(out, r);
        return {};
    }

    if (starts_with_keyword(s, "WRITE")) {
        auto rest = after_keyword(s, 5);
        size_t sp = rest.find_first_of(" \t");
        if (sp == std::string_view::npos) {
            return err("expected fd and string in WRITE");
        }
        auto fd_tok = trim(rest.substr(0, sp));
        auto str_tok = trim(rest.substr(sp + 1));
        TRY_FD(fd, fd_tok)
        size_t pos = 0;
        auto str = parse_quoted(str_tok, pos);
        if (!str) {
            return err("expected quoted string in WRITE");
        }
        if (str->size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            return err("string too long in WRITE");
        }
        write_u8(out, opcode_to_byte(Opcode::WRITE));
        write_u8(out, fd);
        write_u32(out, static_cast<uint32_t>(str->size()));
        for (char c : *str) {
            write_u8(out, static_cast<uint8_t>(c));
        }
        return {};
    }
    if (starts_with_keyword(s, "PRINT")) {
        auto tok = trim(after_keyword(s, 5));
        uint8_t c = 0;
        if (tok.size() >= 3 && tok[0] == '\'') {
            c = static_cast<uint8_t>(tok[1]);
        } else {
            auto v = parse_u32(tok);
            if (!v) {
                return err("invalid operand in PRINT");
            }
            c = static_cast<uint8_t>(*v);
        }
        write_u8(out, opcode_to_byte(Opcode::PRINT));
        write_u8(out, c);
        return {};
    }
    if (starts_with_keyword(s, "NEWLINE")) {
        write_u8(out, opcode_to_byte(Opcode::NEWLINE));
        return {};
    }
    if (starts_with_keyword(s, "PRINTREG")) {
        TRY_REG(r, after_keyword(s, 8))
        write_u8(out, opcode_to_byte(Opcode::PRINTREG));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "EPRINTREG")) {
        TRY_REG(r, after_keyword(s, 9))
        write_u8(out, opcode_to_byte(Opcode::EPRINTREG));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "PRINTCHAR")) {
        TRY_REG(r, after_keyword(s, 9))
        write_u8(out, opcode_to_byte(Opcode::PRINTCHAR));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "EPRINTCHAR")) {
        TRY_REG(r, after_keyword(s, 10))
        write_u8(out, opcode_to_byte(Opcode::EPRINTCHAR));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "READSTR")) {
        TRY_REG(r, after_keyword(s, 7))
        write_u8(out, opcode_to_byte(Opcode::READSTR));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "READCHAR")) {
        TRY_REG(r, after_keyword(s, 8))
        write_u8(out, opcode_to_byte(Opcode::READCHAR));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "READ")) {
        TRY_REG(r, after_keyword(s, 4))
        write_u8(out, opcode_to_byte(Opcode::READ));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "FOPEN")) {
        auto rest = after_keyword(s, 5);
        auto [mode_tok, rest2] = split_comma(rest);
        auto [fd_tok, rest3] = split_comma(rest2);
        mode_tok = trim(mode_tok);
        TRY_FD(fd, fd_tok)
        size_t pos = 0;
        auto fname = parse_quoted(rest3, pos);
        if (!fname) {
            return err("expected quoted filename in FOPEN");
        }
        if (fname->size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            return err("filename too long in FOPEN");
        }

        uint8_t mode_byte = 0;
        if (mode_tok == "r" || mode_tok == "R") {
            mode_byte = 0;
        } else if (mode_tok == "w" || mode_tok == "W") {
            mode_byte = 1;
        } else if (mode_tok == "a" || mode_tok == "A") {
            mode_byte = 2;
        } else {
            return err("invalid mode in FOPEN");
        }

        write_u8(out, opcode_to_byte(Opcode::FOPEN));
        write_u8(out, mode_byte);
        write_u8(out, fd);
        write_u32(out, static_cast<uint32_t>(fname->size()));
        for (char c : *fname) {
            write_u8(out, static_cast<uint8_t>(c));
        }
        return {};
    }
    if (starts_with_keyword(s, "FCLOSE")) {
        TRY_FD(fd, after_keyword(s, 6))
        write_u8(out, opcode_to_byte(Opcode::FCLOSE));
        write_u8(out, fd);
        return {};
    }
    if (starts_with_keyword(s, "FREAD")) {
        auto [fd_tok, reg_tok] = split_comma(after_keyword(s, 5));
        TRY_FD(fd, fd_tok) TRY_REG(r, reg_tok) write_u8(out, opcode_to_byte(Opcode::FREAD));
        write_u8(out, fd);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "FWRITE_REG")) {
        auto [fd_tok, reg_tok] = split_comma(after_keyword(s, 10));
        TRY_FD(fd, fd_tok) TRY_REG(r, reg_tok) write_u8(out, opcode_to_byte(Opcode::FWRITE_REG));
        write_u8(out, fd);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "FWRITE_IMM")) {
        auto [fd_tok, val_tok] = split_comma(after_keyword(s, 10));
        TRY_FD(fd, fd_tok)
        auto v = parse_i32(val_tok);
        if (!v) {
            return err("invalid immediate in FWRITE_IMM");
        }
        write_u8(out, opcode_to_byte(Opcode::FWRITE_IMM));
        write_u8(out, fd);
        write_i32(out, *v);
        return {};
    }
    if (starts_with_keyword(s, "FWRITE")) {
        auto [fd_tok, value_tok] = split_comma(after_keyword(s, 6));
        TRY_FD(fd, fd_tok)

        if (auto reg = parse_register(value_tok)) {
            write_u8(out, opcode_to_byte(Opcode::FWRITE_REG));
            write_u8(out, fd);
            write_u8(out, *reg);
            return {};
        }

        auto v = parse_i32(value_tok);
        if (!v) {
            auto tok = trim(value_tok);
            if (!tok.empty() && tok.front() == '"') {
                return err("invalid FWRITE operand: expected register or numeric immediate (quoted "
                           "strings are not supported)");
            }
            return err("invalid FWRITE operand: expected register or numeric immediate");
        }

        write_u8(out, opcode_to_byte(Opcode::FWRITE_IMM));
        write_u8(out, fd);
        write_i32(out, *v);
        return {};
    }
    if (starts_with_keyword(s, "FSEEK_REG")) {
        auto [fd_tok, reg_tok] = split_comma(after_keyword(s, 9));
        TRY_FD(fd, fd_tok) TRY_REG(r, reg_tok) write_u8(out, opcode_to_byte(Opcode::FSEEK_REG));
        write_u8(out, fd);
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "FSEEK_IMM")) {
        auto [fd_tok, val_tok] = split_comma(after_keyword(s, 9));
        TRY_FD(fd, fd_tok)
        auto v = parse_i32(val_tok);
        if (!v) {
            return err("invalid offset in FSEEK");
        }
        write_u8(out, opcode_to_byte(Opcode::FSEEK_IMM));
        write_u8(out, fd);
        write_i32(out, *v);
        return {};
    }
    if (starts_with_keyword(s, "FSEEK")) {
        auto [fd_tok, value_tok] = split_comma(after_keyword(s, 5));
        TRY_FD(fd, fd_tok)

        if (auto reg = parse_register(value_tok)) {
            write_u8(out, opcode_to_byte(Opcode::FSEEK_REG));
            write_u8(out, fd);
            write_u8(out, *reg);
            return {};
        }

        auto v = parse_i32(value_tok);
        if (!v) {
            return err("invalid offset in FSEEK");
        }

        write_u8(out, opcode_to_byte(Opcode::FSEEK_IMM));
        write_u8(out, fd);
        write_i32(out, *v);
        return {};
    }

    if (starts_with_keyword(s, "EXEC")) {
        auto rest = after_keyword(s, 4);
        size_t pos = 0;
        auto cmd = parse_quoted(rest, pos);
        if (!cmd) {
            return err("expected quoted command in EXEC");
        }
        if (cmd->size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            return err("command too long in EXEC");
        }
        auto after = trim(rest.substr(pos));
        if (after.empty() || after[0] != ',') {
            return err("expected register after command in EXEC");
        }
        TRY_REG(r, after.substr(1))
        write_u8(out, opcode_to_byte(Opcode::EXEC));
        write_u8(out, r);
        write_u32(out, static_cast<uint32_t>(cmd->size()));
        for (char c : *cmd) {
            write_u8(out, static_cast<uint8_t>(c));
        }
        return {};
    }
    if (starts_with_keyword(s, "SLEEP")) {
        auto operand = trim(after_keyword(s, 5));

        if (auto reg = parse_register(operand)) {
            write_u8(out, opcode_to_byte(Opcode::SLEEP_REG));
            write_u8(out, *reg);
            return {};
        }

        auto v = parse_u32(operand);
        if (!v) {
            return err("invalid operand in SLEEP (expected register or u32 immediate)");
        }
        write_u8(out, opcode_to_byte(Opcode::SLEEP));
        write_u32(out, *v);
        return {};
    }
    if (starts_with_keyword(s, "RAND")) {
        auto rest = after_keyword(s, 4);
        auto [reg_tok, ranges] = split_comma(rest);
        TRY_REG(r, reg_tok)
        int64_t min_val = INT64_MIN;
        int64_t max_val = INT64_MAX;
        if (!ranges.empty()) {
            auto [min_tok, max_tok] = split_comma(ranges);
            auto mn = parse_i64(min_tok);
            auto mx = parse_i64(max_tok);
            if (!mn || !mx) {
                return err("invalid range in RAND");
            }
            min_val = *mn;
            max_val = *mx;
        }
        write_u8(out, opcode_to_byte(Opcode::RAND));
        write_u8(out, r);
        write_i64(out, min_val);
        write_i64(out, max_val);
        return {};
    }
    if (starts_with_keyword(s, "GETKEY")) {
        TRY_REG(r, after_keyword(s, 6))
        write_u8(out, opcode_to_byte(Opcode::GETKEY));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "CLRSCR")) {
        write_u8(out, opcode_to_byte(Opcode::CLRSCR));
        return {};
    }
    if (starts_with_keyword(s, "GETARGC")) {
        TRY_REG(r, after_keyword(s, 7))
        write_u8(out, opcode_to_byte(Opcode::GETARGC));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "GETARG")) {
        auto [reg_tok, idx_tok] = split_comma(after_keyword(s, 6));
        TRY_REG(r, reg_tok)
        auto idx = parse_u32(idx_tok);
        if (!idx) {
            return err("invalid index in GETARG");
        }
        write_u8(out, opcode_to_byte(Opcode::GETARG));
        write_u8(out, r);
        write_u32(out, *idx);
        return {};
    }
    if (starts_with_keyword(s, "GETENV")) {
        auto [reg_tok, name_tok] = split_comma(after_keyword(s, 6));
        TRY_REG(r, reg_tok)
        name_tok = trim(name_tok);
        std::string_view name = name_tok;
        if (!name.empty() && name[0] == '"') {
            size_t pos = 0;
            auto q = parse_quoted(name_tok, pos);
            if (!q) {
                return err("unterminated string in GETENV");
            }
            name = *q;
        }
        if (name.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
            return err("env var name too long in GETENV");
        }
        write_u8(out, opcode_to_byte(Opcode::GETENV));
        write_u8(out, r);
        write_u32(out, static_cast<uint32_t>(name.size()));
        for (char c : name) {
            write_u8(out, static_cast<uint8_t>(c));
        }
        return {};
    }

    if (starts_with_keyword(s, "DROPPRIV")) {
        write_u8(out, opcode_to_byte(Opcode::DROPPRIV));
        return {};
    }
    if (starts_with_keyword(s, "GETMODE")) {
        TRY_REG(r, after_keyword(s, 7))
        write_u8(out, opcode_to_byte(Opcode::GETMODE));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "REGSYSCALL")) {
        auto [id_tok, label_tok] = split_comma(after_keyword(s, 10));
        auto id = parse_u32(id_tok);
        if (!id || *id >= MAX_SYSCALLS) {
            return err("invalid syscall id in REGSYSCALL");
        }
        TRY_LABEL(addr, label_tok)
        write_u8(out, opcode_to_byte(Opcode::REGSYSCALL));
        write_u8(out, static_cast<uint8_t>(*id));
        write_u32(out, addr);
        return {};
    }
    if (starts_with_keyword(s, "SYSCALL")) {
        auto id = parse_u32(after_keyword(s, 7));
        if (!id || *id >= MAX_SYSCALLS) {
            return err("invalid syscall id in SYSCALL");
        }
        write_u8(out, opcode_to_byte(Opcode::SYSCALL));
        write_u8(out, static_cast<uint8_t>(*id));
        return {};
    }
    if (starts_with_keyword(s, "SYSRET")) {
        write_u8(out, opcode_to_byte(Opcode::SYSRET));
        return {};
    }
    if (starts_with_keyword(s, "REGFAULT")) {
        auto [id_tok, label_tok] = split_comma(after_keyword(s, 8));
        auto id = parse_u32(id_tok);
        if (!id) {
            return err("invalid fault id in REGFAULT");
        }
        TRY_LABEL(addr, label_tok)
        write_u8(out, opcode_to_byte(Opcode::REGFAULT));
        write_u8(out, static_cast<uint8_t>(*id));
        write_u32(out, addr);
        return {};
    }
    if (starts_with_keyword(s, "FAULTRET")) {
        write_u8(out, opcode_to_byte(Opcode::FAULTRET));
        return {};
    }
    if (starts_with_keyword(s, "GETFAULT")) {
        TRY_REG(r, after_keyword(s, 8))
        write_u8(out, opcode_to_byte(Opcode::GETFAULT));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "SETPERM")) {
        auto rest = after_keyword(s, 7);
        auto [start_tok, rest2] = split_comma(rest);
        auto [count_tok, rest3] = split_comma(rest2);
        auto perm_tok = trim(rest3);

        auto start = parse_u32(start_tok);
        auto count = parse_u32(count_tok);
        if (!start || !count) {
            return err("invalid start/count in SETPERM");
        }

        // RW/RW
        size_t slash = perm_tok.find('/');
        if (slash == std::string_view::npos) {
            return err("expected priv/prot in SETPERM");
        }
        auto priv_str = perm_tok.substr(0, slash);
        auto prot_str = perm_tok.substr(slash + 1);

        uint8_t priv_r = priv_str.find('R') != std::string_view::npos ? 1 : 0;
        uint8_t priv_w = priv_str.find('W') != std::string_view::npos ? 1 : 0;
        uint8_t prot_r = prot_str.find('R') != std::string_view::npos ? 1 : 0;
        uint8_t prot_w = prot_str.find('W') != std::string_view::npos ? 1 : 0;

        write_u8(out, opcode_to_byte(Opcode::SETPERM));
        write_u32(out, *start);
        write_u32(out, *count);
        write_u8(out, priv_r);
        write_u8(out, priv_w);
        write_u8(out, prot_r);
        write_u8(out, prot_w);
        return {};
    }

    if (starts_with_keyword(s, "BREAK")) {
        write_u8(out, opcode_to_byte(Opcode::BREAK));
        return {};
    }
    if (starts_with_keyword(s, "CONTINUE")) {
        write_u8(out, opcode_to_byte(Opcode::CONTINUE));
        return {};
    }
    if (starts_with_keyword(s, "DUMPREGS")) {
        write_u8(out, opcode_to_byte(Opcode::DUMPREGS));
        return {};
    }
    if (starts_with_keyword(s, "PRINT_STACKSIZE")) {
        write_u8(out, opcode_to_byte(Opcode::PRINT_STACKSIZE));
        return {};
    }

    return std::unexpected(std::format("Unknown instruction '{}'", s));

#undef TRY_REG
#undef TRY_FD
#undef TRY_LABEL
#undef TRY_DATA
}
} // namespace bbxc::encoder