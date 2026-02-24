#pragma once
#include <stddef.h>
#include <stdint.h>

const char *opcode_name(uint8_t op);
void print_regs(const int64_t *regs, int count);
void print_stack(const int64_t *stack, size_t sp);