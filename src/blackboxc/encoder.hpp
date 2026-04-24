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

size_t instr_size(std::string_view line);

struct Operand {
    enum class Kind { Reg, Imm, Imm64, Bss, BssRef, Var, Data, Label, FD };
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
};

// size of one encoded operand (type byte + value bytes)
size_t operand_encoded_size(Operand::Kind kind);

std::expected<Operand, std::string> parse_operand(std::string_view tok, const OperandContext& ctx);

void encode_operand(const Operand& op, std::vector<uint8_t>& out);
// encode one line
std::expected<void, std::string> encode(std::string_view line, const OperandContext& ctx,
                                        std::vector<uint8_t>& out, bool debug);

void write_u8(std::vector<uint8_t>& buf, uint8_t v);
void write_u16(std::vector<uint8_t>& buf, uint16_t v);
void write_u32(std::vector<uint8_t>& buf, uint32_t v);
void write_u64(std::vector<uint8_t>& buf, uint64_t v);
void write_i32(std::vector<uint8_t>& buf, int32_t v);
void write_i64(std::vector<uint8_t>& buf, int64_t v);

} // namespace bbxc::encoder
#endif // BLACKBOX_ENCODER_HPP
