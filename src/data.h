#pragma once

#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <vector>
#include "define.h"
namespace blackbox {
namespace data {
void write_u32(FILE *out, uint32_t val);
uint32_t read_u32(const uint8_t *data, size_t *pc);

void write_u64(FILE *out, uint64_t val);
uint64_t read_u64(const uint8_t *data, size_t *pc);

void write_i64(FILE *out, int64_t val);
int64_t read_i64(const uint8_t *data, size_t *pc);

bool read_u8(const std::vector<uint8_t> &bytes, size_t index, uint8_t &out);
bool read_u32_le(const std::vector<uint8_t> &bytes, size_t index, uint32_t &out);
bool read_i32_le(const std::vector<uint8_t> &bytes, size_t index, int32_t &out);
bool read_u64_le(const std::vector<uint8_t> &bytes, size_t index, uint64_t &out);

}}
