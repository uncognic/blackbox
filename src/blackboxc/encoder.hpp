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
#include <vector>

namespace bbxc::encoder {

size_t instr_size(std::string_view line);

// encode one line
std::expected<void, std::string> encode(std::string_view line,
                                        const std::vector<asm_helpers::Label>& labels,
                                        const std::vector<asm_helpers::DataEntry>& data_entries,
                                        std::vector<uint8_t>& out, bool debug);

std::optional<uint8_t> parse_register(std::string_view name);
std::optional<uint8_t> parse_file_descriptor(std::string_view name);

void write_u8(std::vector<uint8_t>& buf, uint8_t v);
void write_u16(std::vector<uint8_t>& buf, uint16_t v);
void write_u32(std::vector<uint8_t>& buf, uint32_t v);
void write_u64(std::vector<uint8_t>& buf, uint64_t v);
void write_i32(std::vector<uint8_t>& buf, int32_t v);
void write_i64(std::vector<uint8_t>& buf, int64_t v);

} // namespace bbxc::encoder
#endif // BLACKBOX_ENCODER_HPP
