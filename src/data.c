#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>
#include "define.h"
void write_u32(FILE *out, uint32_t val)
{
    fputc((val >> 0) & 0xFF, out);
    fputc((val >> 8) & 0xFF, out);
    fputc((val >> 16) & 0xFF, out);
    fputc((val >> 24) & 0xFF, out);
}
void write_u64(FILE *out, uint64_t val)
{
    fputc((val >> 0) & 0xFF, out);
    fputc((val >> 8) & 0xFF, out);
    fputc((val >> 16) & 0xFF, out);
    fputc((val >> 24) & 0xFF, out);
    fputc((val >> 32) & 0xFF, out);
    fputc((val >> 40) & 0xFF, out);
    fputc((val >> 48) & 0xFF, out);
    fputc((val >> 56) & 0xFF, out);
}
void write_i64(FILE *out, int64_t val)
{
    write_u64(out, (uint64_t)val);
}
int64_t read_i64(const uint8_t *data, size_t *pc)
{
    uint64_t b0 = (uint64_t)data[*pc + 0];
    uint64_t b1 = (uint64_t)data[*pc + 1];
    uint64_t b2 = (uint64_t)data[*pc + 2];
    uint64_t b3 = (uint64_t)data[*pc + 3];
    uint64_t b4 = (uint64_t)data[*pc + 4];
    uint64_t b5 = (uint64_t)data[*pc + 5];
    uint64_t b6 = (uint64_t)data[*pc + 6];
    uint64_t b7 = (uint64_t)data[*pc + 7];

    uint64_t u = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24) | (b4 << 32) | (b5 << 40) | (b6 << 48) | (b7 << 56);

    *pc += 8;
    return (int64_t)u;
}
uint64_t read_u64(const uint8_t *data, size_t *pc)
{
    uint64_t b0 = (uint64_t)data[*pc + 0];
    uint64_t b1 = (uint64_t)data[*pc + 1];
    uint64_t b2 = (uint64_t)data[*pc + 2];
    uint64_t b3 = (uint64_t)data[*pc + 3];
    uint64_t b4 = (uint64_t)data[*pc + 4];
    uint64_t b5 = (uint64_t)data[*pc + 5];
    uint64_t b6 = (uint64_t)data[*pc + 6];
    uint64_t b7 = (uint64_t)data[*pc + 7];

    uint64_t u = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24) | (b4 << 32) | (b5 << 40) | (b6 << 48) | (b7 << 56);

    *pc += 8;
    return u;
}
uint32_t read_u32(const uint8_t *data, size_t *pc)
{
    uint32_t b0 = (uint32_t)data[*pc + 0];
    uint32_t b1 = (uint32_t)data[*pc + 1];
    uint32_t b2 = (uint32_t)data[*pc + 2];
    uint32_t b3 = (uint32_t)data[*pc + 3];

    uint32_t u = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);

    *pc += 4;
    return u;
}