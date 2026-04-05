#include <cstdint>
#include <cstdio>
#include <array>
#include <vector>
#include "data.h"
namespace blackbox {
namespace data {

static void write_bytes(FILE *out, const std::array<uint8_t, 4> &bytes)
{
    fwrite(bytes.data(), 1, bytes.size(), out);
}

static void write_bytes(FILE *out, const std::array<uint8_t, 8> &bytes)
{
    fwrite(bytes.data(), 1, bytes.size(), out);
}

void write_u32(FILE *out, uint32_t val)
{
    const std::array<uint8_t, 4> bytes = {
        static_cast<uint8_t>(val >> 0),
        static_cast<uint8_t>(val >> 8),
        static_cast<uint8_t>(val >> 16),
        static_cast<uint8_t>(val >> 24),
    };
    write_bytes(out, bytes);
}

void write_u64(FILE *out, uint64_t val)
{
    const std::array<uint8_t, 8> bytes = {
        static_cast<uint8_t>(val >> 0),
        static_cast<uint8_t>(val >> 8),
        static_cast<uint8_t>(val >> 16),
        static_cast<uint8_t>(val >> 24),
        static_cast<uint8_t>(val >> 32),
        static_cast<uint8_t>(val >> 40),
        static_cast<uint8_t>(val >> 48),
        static_cast<uint8_t>(val >> 56),
    };
    write_bytes(out, bytes);
}

void write_i64(FILE *out, int64_t val)
{
    write_u64(out, static_cast<uint64_t>(val));
}

static uint64_t read_le_uint(const uint8_t *data, size_t *pc, size_t size)
{
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i)
    {
        value |= static_cast<uint64_t>(data[*pc + i]) << (i * 8);
    }
    *pc += size;
    return value;
}

int64_t read_i64(const uint8_t *data, size_t *pc)
{
    return static_cast<int64_t>(read_le_uint(data, pc, 8));
}

uint64_t read_u64(const uint8_t *data, size_t *pc)
{
    return read_le_uint(data, pc, 8);
}

uint32_t read_u32(const uint8_t *data, size_t *pc)
{
    return static_cast<uint32_t>(read_le_uint(data, pc, 4));
}

bool read_u8(const std::vector<uint8_t> &bytes, size_t index, uint8_t &out)
{
    if (index >= bytes.size()) {
        return false;
    }
    out = bytes[index];
    return true;
}

bool read_u32_le(const std::vector<uint8_t> &bytes, size_t index, uint32_t &out)
{
    if (index + 4 > bytes.size()) {
        return false;
    }
    out = static_cast<uint32_t>(bytes[index]) |
          (static_cast<uint32_t>(bytes[index + 1]) << 8U) |
          (static_cast<uint32_t>(bytes[index + 2]) << 16U) |
          (static_cast<uint32_t>(bytes[index + 3]) << 24U);
    return true;
}

bool read_i32_le(const std::vector<uint8_t> &bytes, size_t index, int32_t &out)
{
    uint32_t tmp = 0;
    if (!read_u32_le(bytes, index, tmp)) {
        return false;
    }
    out = static_cast<int32_t>(tmp);
    return true;
}

bool read_u64_le(const std::vector<uint8_t> &bytes, size_t index, uint64_t &out)
{
    if (index + 8 > bytes.size()) {
        return false;
    }
    out = static_cast<uint64_t>(bytes[index]) |
          (static_cast<uint64_t>(bytes[index + 1]) << 8U) |
          (static_cast<uint64_t>(bytes[index + 2]) << 16U) |
          (static_cast<uint64_t>(bytes[index + 3]) << 24U) |
          (static_cast<uint64_t>(bytes[index + 4]) << 32U) |
          (static_cast<uint64_t>(bytes[index + 5]) << 40U) |
          (static_cast<uint64_t>(bytes[index + 6]) << 48U) |
          (static_cast<uint64_t>(bytes[index + 7]) << 56U);
    return true;
}
}}