//
// Created by User on 2026-04-19.
//

#ifndef BLACKBOX_ENCODER_HPP
#define BLACKBOX_ENCODER_HPP
#include "asm_util.hpp"
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bbxc::encoder {



struct Operand {
    enum class Kind { Reg, Imm, Imm64, Bss, BssRef, Var, Data, Label, FD, HeapAddr, HeapReg };
    Kind kind = Kind::Imm;
    uint8_t reg = 0;
    int32_t imm = 0;
    int64_t imm64 = 0;
    uint32_t idx = 0;
    std::string name;
};

struct OperandContext {
    const std::vector<asm_helpers::Label>& labels;
    const std::vector<asm_helpers::DataEntry>& data_entries;
    const std::unordered_map<std::string, uint32_t>& bss_symbols;
    bool sizing_pass = false;
};
size_t instr_size(std::string_view line, const OperandContext& ctx);
struct CountingBuffer {
    size_t n = 0;
    void push_back(uint8_t) { n++; }
    size_t size() const { return n; }
};

// size of one encoded operand (type byte + value bytes)
size_t operand_encoded_size(Operand::Kind kind);

std::expected<Operand, std::string> parse_operand(std::string_view tok, const OperandContext& ctx);

template <typename Buf> void encode_operand(const Operand& op, Buf& out);
// encode one line
template <typename Buf>
std::expected<void, std::string> encode(std::string_view line, const OperandContext& ctx, Buf& out,
                                        bool debug = false);

template <typename Buf> void write_u8(Buf& buf, uint8_t v);
template <typename Buf> void write_u16(Buf& buf, uint16_t v);
template <typename Buf> void write_u32(Buf& buf, uint32_t v);
template <typename Buf> void write_u64(Buf& buf, uint64_t v);
template <typename Buf> void write_i32(Buf& buf, int32_t v);
template <typename Buf> void write_i64(Buf& buf, int64_t v);

} // namespace bbxc::encoder
#endif // BLACKBOX_ENCODER_HPP
