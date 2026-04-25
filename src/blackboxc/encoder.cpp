//
// Created by User on 2026-04-19.
//

#include "encoder.hpp"
#include "../define.hpp"
#include <charconv>
#include <format>
#include <limits>
#include <print>
#include <string>

namespace bbxc::encoder {

size_t operand_encoded_size(Operand::Kind kind) {
    switch (kind) {
        case Operand::Kind::Reg:
            return 2;
        case Operand::Kind::Imm:
            return 5;
        case Operand::Kind::Imm64:
            return 9;
        case Operand::Kind::Bss:
            return 5;
        case Operand::Kind::BssRef:
            return 5;
        case Operand::Kind::Var:
            return 5;
        case Operand::Kind::Data:
            return 5;
        case Operand::Kind::Label:
            return 5;
        case Operand::Kind::FD:
            return 2;
        default:
            return 0;
    }
}

template <typename Buf> void encode_operand(const Operand& op, Buf& out) {
    switch (op.kind) {
        case Operand::Kind::Reg:
            write_u8(out, static_cast<uint8_t>(OperandType::Reg));
            write_u8(out, op.reg);
            break;
        case Operand::Kind::Imm:
            write_u8(out, static_cast<uint8_t>(OperandType::Imm));
            write_i32(out, op.imm);
            break;
        case Operand::Kind::Imm64:
            write_u8(out, static_cast<uint8_t>(OperandType::Imm64));
            write_i64(out, op.imm64);
            break;
        case Operand::Kind::Bss:
            write_u8(out, static_cast<uint8_t>(OperandType::Bss));
            write_u32(out, op.idx);
            break;
        case Operand::Kind::BssRef:
            write_u8(out, static_cast<uint8_t>(OperandType::BssRef));
            write_u32(out, op.idx);
            break;
        case Operand::Kind::Var:
            write_u8(out, static_cast<uint8_t>(OperandType::Var));
            write_u32(out, op.idx);
            break;
        case Operand::Kind::Data:
            write_u8(out, static_cast<uint8_t>(OperandType::Data));
            write_u32(out, op.idx);
            break;
        case Operand::Kind::Label:
            write_u8(out, static_cast<uint8_t>(OperandType::Imm));
            write_u32(out, op.idx);
            break;
        case Operand::Kind::FD:
            write_u8(out, static_cast<uint8_t>(OperandType::Reg)); // fd reuses reg slot
            write_u8(out, op.reg);
            break;
    }
}

// writers
template <typename Buf> void write_u8(Buf& buf, uint8_t v) {
    buf.push_back(v);
}

template <typename Buf> void write_u16(Buf& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
}

template <typename Buf> void write_u32(Buf& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v));
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >> 24));
}

template <typename Buf> void write_u64(Buf& buf, uint64_t v) {
    write_u32(buf, static_cast<uint32_t>(v));
    write_u32(buf, static_cast<uint32_t>(v >> 32));
}

template <typename Buf> void write_i32(Buf& buf, int32_t v) {
    write_u32(buf, static_cast<uint32_t>(v));
}

template <typename Buf> void write_i64(Buf& buf, int64_t v) {
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
namespace {
std::string_view trim(std::string_view s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::string_view strip_comment(std::string_view s) {
    size_t pos = s.find(';');
    if (pos != std::string_view::npos) {
        s = s.substr(0, pos);
    }
    return trim(s);
}
bool starts_with_keyword(std::string_view s, std::string_view kw) {
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

std::string_view after_keyword(std::string_view s, size_t kw_len) {
    s.remove_prefix(kw_len);
    return trim(s);
}

// parse quoted string and move pos
std::optional<std::string_view> parse_quoted(std::string_view s, size_t& pos) {
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

std::optional<int32_t> parse_i32(std::string_view s) {
    s = trim(s);
    int32_t v = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

std::optional<uint32_t> parse_u32(std::string_view s) {
    s = trim(s);
    uint32_t v = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

std::optional<int64_t> parse_i64(std::string_view s) {
    s = trim(s);
    int64_t v = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), v);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}
// split on first comma, returning lhs and rhs trimmed
std::pair<std::string_view, std::string_view> split_comma(std::string_view s) {
    size_t pos = s.find(',');
    if (pos == std::string_view::npos) {
        return {trim(s), {}};
    }
    return {trim(s.substr(0, pos)), trim(s.substr(pos + 1))};
}

// find label addr by name
std::optional<uint32_t> resolve_label(std::string_view name,
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
std::optional<uint32_t> resolve_data(std::string_view name,
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
size_t quoted_string_len(std::string_view s) {
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
static std::string_view strip_brackets(std::string_view s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
        s.remove_prefix(1);
        s.remove_suffix(1);
        return trim(s);
    }
    return s;
}

constexpr bool is_writable(Operand::Kind k) {
    return k == Operand::Kind::Reg || k == Operand::Kind::Bss || k == Operand::Kind::Var;
}

} // namespace

std::expected<Operand, std::string> parse_operand(std::string_view tok, const OperandContext& ctx) {
    tok = trim(tok);
    if (tok.empty()) {
        return std::unexpected("empty operand");
    }

    // BssDeref: [name]
    if (tok.front() == '[' && tok.back() == ']') {
        auto name = trim(tok.substr(1, tok.size() - 2));
        auto it = ctx.bss_symbols.find(std::string(name));
        if (it == ctx.bss_symbols.end()) {
            return std::unexpected(std::format("undefined bss symbol '{}'", name));
        }
        Operand op;
        op.kind = Operand::Kind::Bss;
        op.idx = it->second;
        op.name = std::string(name);
        return op;
    }

    // FD: STDIN/STDOUT/STDERR/Fxx
    if (auto fd = parse_file_descriptor(tok)) {
        Operand op;
        op.kind = Operand::Kind::FD;
        op.reg = *fd;
        op.name = std::string(tok);
        return op;
    }

    // Reg: Rxx
    if (auto reg = parse_register(tok)) {
        Operand op;
        op.kind = Operand::Kind::Reg;
        op.reg = *reg;
        op.name = std::string(tok);
        return op;
    }

    // BssDeref w/o brackets (addr of)
    {
        auto it = ctx.bss_symbols.find(std::string(tok));
        if (it != ctx.bss_symbols.end()) {
            Operand op;
            op.kind = Operand::Kind::BssRef;
            op.idx = it->second;
            op.name = std::string(tok);
            return op;
        }
    }

    // Data: $name
    if (tok.front() == '$') {
        auto name = tok.substr(1);
        auto idx = resolve_data(name, ctx.data_entries);
        if (!idx) {
            return std::unexpected(std::format("undefined data entry '{}'", tok));
        }
        Operand op;
        op.kind = Operand::Kind::Data;
        op.idx = *idx;
        op.name = std::string(tok);
        return op;
    }

    if (starts_with_keyword(tok, "VAR")) {
        auto rest = trim(tok.substr(3));
        auto slot = parse_u32(rest);
        if (!slot) {
            return std::unexpected(std::format("invalid VAR slot '{}'", rest));
        }
        Operand op;
        op.kind = Operand::Kind::Var;
        op.idx = *slot;
        op.name = std::string(tok);
        return op;
    }

    {
        auto addr = resolve_label(tok, ctx.labels);
        if (addr) {
            Operand op;
            op.kind = Operand::Kind::Label;
            op.idx = *addr;
            op.name = std::string(tok);
            return op;
        }
    }

    // imm i32
    if (auto imm = parse_i32(tok)) {
        Operand op;
        op.kind = Operand::Kind::Imm;
        op.imm = *imm;
        op.name = std::string(tok);
        return op;
    }

    return std::unexpected(std::format("unrecognized operand '{}'", tok));
}

size_t instr_size(std::string_view line, const OperandContext& ctx) {
    OperandContext lenient = ctx;
    lenient.sizing_pass = true;
    CountingBuffer buf;
    static_cast<void>(encode(line, lenient, buf));
    return buf.n;
}
template <typename Buf>
std::expected<void, std::string> encode(std::string_view line, const OperandContext& ctx, Buf& out,
                                        bool debug) {
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
        auto addr = resolve_label(name, ctx.labels);
        if (!addr) {
            if (ctx.sizing_pass) {
                return 0u;
            }
            return std::unexpected(std::format("undefined label '{}' in '{}'", name, s));
        }
        return *addr;
    };
    auto need_data = [&](std::string_view name) -> std::expected<uint32_t, std::string> {
        auto idx = resolve_data(name, ctx.data_entries);
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
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("ADD dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::ADD));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "SUB")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("SUB dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::SUB));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "MUL")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("MUL dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::MUL));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "DIV")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("DIV dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::DIV));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "MOD")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("MOD dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::MOD));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "INC")) {
        auto dst = parse_operand(trim(after_keyword(s, 3)), ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }
        if (!is_writable(dst->kind)) {
            return err("INC dst must be register, [bss] or framevar");
        }
        write_u8(out, opcode_to_byte(Opcode::INC));
        encode_operand(*dst, out);
        return {};
    }
    if (starts_with_keyword(s, "DEC")) {
        auto dst = parse_operand(trim(after_keyword(s, 3)), ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }
        if (!is_writable(dst->kind)) {
            return err("DEC dst must be register, [bss] or framevar");
        }
        write_u8(out, opcode_to_byte(Opcode::DEC));
        encode_operand(*dst, out);
        return {};
    }

    if (starts_with_keyword(s, "AND")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("AND dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::AND));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "OR")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("OR dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::OR));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "XOR")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("XOR dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::XOR));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "NOT")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("NOT dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::NOT));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "SHL")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst_tok)
        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }
        if (src->kind == Operand::Kind::Imm) {
            Operand promoted = *src;
            promoted.kind = Operand::Kind::Imm64;
            promoted.imm64 = static_cast<int64_t>(src->imm);
            write_u8(out, opcode_to_byte(Opcode::SHL));
            write_u8(out, d);
            encode_operand(promoted, out);
            return {};
        }
        if (src->kind != Operand::Kind::Reg) {
            return err("SHL source must be register or immediate");
        }
        write_u8(out, opcode_to_byte(Opcode::SHL));
        write_u8(out, d);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "SHR")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));
        TRY_REG(d, dst_tok)
        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }
        if (src->kind == Operand::Kind::Imm) {
            Operand promoted = *src;
            promoted.kind = Operand::Kind::Imm64;
            promoted.imm64 = static_cast<int64_t>(src->imm);
            write_u8(out, opcode_to_byte(Opcode::SHR));
            write_u8(out, d);
            encode_operand(promoted, out);
            return {};
        }
        if (src->kind != Operand::Kind::Reg) {
            return err("SHR source must be register or immediate");
        }
        write_u8(out, opcode_to_byte(Opcode::SHR));
        write_u8(out, d);
        encode_operand(*src, out);
        return {};
    }

    if (starts_with_keyword(s, "MOV")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));
        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }
        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        // validate: dst must be writable (Reg or Bss)
        if (dst->kind != Operand::Kind::Reg && dst->kind != Operand::Kind::Bss &&
            dst->kind != Operand::Kind::Var) {
            return err("MOV dst must be a register, [bss], or frame var");
        }

        write_u8(out, opcode_to_byte(Opcode::MOV));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "PUSH")) {
        auto src = parse_operand(trim(after_keyword(s, 4)), ctx);
        if (!src) {
            return std::unexpected(src.error());
        }
        if (src->kind != Operand::Kind::Reg && src->kind != Operand::Kind::Imm) {
            return err("PUSH expects register or immediate");
        }
        write_u8(out, opcode_to_byte(Opcode::PUSH));
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "POP")) {
        TRY_REG(r, after_keyword(s, 3))
        write_u8(out, opcode_to_byte(Opcode::POP));
        write_u8(out, r);
        return {};
    }
    if (starts_with_keyword(s, "CMP")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("CMP dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::CMP));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }

    if (starts_with_keyword(s, "JMP")) {
        TRY_LABEL(addr, trim(after_keyword(s, 3)))
        write_u8(out, opcode_to_byte(Opcode::JMP));
        write_u8(out, static_cast<uint8_t>(OperandType::Imm));
        write_i32(out, static_cast<int32_t>(addr));
        return {};
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
            for (auto& l : ctx.labels) {
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
    if (starts_with_keyword(s, "LOAD")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("LOAD dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::LOAD));
        encode_operand(*dst, out);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "STORE")) {
        auto [dst_tok, src_tok] = split_comma(after_keyword(s, 3));

        auto dst = parse_operand(dst_tok, ctx);
        if (!dst) {
            return std::unexpected(dst.error());
        }

        auto src = parse_operand(src_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }

        if (!is_writable(dst->kind)) {
            return err("STORE dst must be register, [bss] or framevar");
        }

        write_u8(out, opcode_to_byte(Opcode::STORE));
        encode_operand(*dst, out);
        encode_operand(*src, out);
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
    if (starts_with_keyword(s, "FWRITE")) {
        auto [fd_tok, val_tok] = split_comma(after_keyword(s, 6));
        TRY_FD(fd, fd_tok)
        auto src = parse_operand(val_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }
        if (src->kind != Operand::Kind::Reg && src->kind != Operand::Kind::Imm) {
            return err("FWRITE value must be register or immediate");
        }
        write_u8(out, opcode_to_byte(Opcode::FWRITE));
        write_u8(out, fd);
        encode_operand(*src, out);
        return {};
    }
    if (starts_with_keyword(s, "FSEEK")) {
        auto [fd_tok, val_tok] = split_comma(after_keyword(s, 5));
        TRY_FD(fd, fd_tok)
        auto src = parse_operand(val_tok, ctx);
        if (!src) {
            return std::unexpected(src.error());
        }
        if (src->kind != Operand::Kind::Reg && src->kind != Operand::Kind::Imm) {
            return err("FSEEK offset must be register or immediate");
        }
        write_u8(out, opcode_to_byte(Opcode::FSEEK));
        write_u8(out, fd);
        encode_operand(*src, out);
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
        auto src = parse_operand(trim(after_keyword(s, 5)), ctx);
        if (!src) {
            return std::unexpected(src.error());
        }
        if (src->kind != Operand::Kind::Reg && src->kind != Operand::Kind::Imm) {
            return err("SLEEP expects register or immediate");
        }
        write_u8(out, opcode_to_byte(Opcode::SLEEP));
        encode_operand(*src, out);
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
template std::expected<void, std::string>
encode<std::vector<uint8_t>>(std::string_view, const OperandContext&, std::vector<uint8_t>&, bool);

template std::expected<void, std::string>
encode<CountingBuffer>(std::string_view, const OperandContext&, CountingBuffer&, bool);
} // namespace bbxc::encoder