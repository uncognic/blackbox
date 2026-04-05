#pragma once

#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cctype>
using std::FILE;

#include "define.h"

#ifdef __cplusplus
extern "C" {
#endif

void write_u32(FILE *out, uint32_t val);
uint32_t read_u32(const uint8_t *data, size_t *pc);

void write_u64(FILE *out, uint64_t val);
uint64_t read_u64(const uint8_t *data, size_t *pc);

void write_i64(FILE *out, int64_t val);
int64_t read_i64(const uint8_t *data, size_t *pc);

#ifdef __cplusplus
}
#endif

