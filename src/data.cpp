#include <cstdint>
#include <cstdio>
#include <array>
#include "data.h"

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
