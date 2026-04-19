//
// Created by User on 2026-04-18.
//

#include "ops_bitwise.hpp"
#include "../vm.hpp"
#include <format>

void VM::op_and() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    regs[dst] &= regs[src];
}

void VM::op_or() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    regs[dst] |= regs[src];
}

void VM::op_xor() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    regs[dst] ^= regs[src];
}

void VM::op_not() {
    size_t reg = fetch_reg();
    regs[reg] = ~regs[reg];
}

void VM::op_shl() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    int64_t shift = regs[src];
    if (shift < 0 || shift >= 64) {
        regs[dst] = 0;
        return;
    }
    regs[dst] <<= shift;
}

void VM::op_shr() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    int64_t shift = regs[src];
    if (shift < 0 || shift >= 64) {
        regs[dst] = regs[dst] < 0 ? -1 : 0;
        return;
    }
    regs[dst] >>= shift;
}

void VM::op_shli() {
    size_t   reg   = fetch_reg();
    uint64_t shift = fetch_u64();
    if (shift >= 64) {
        regs[reg] = 0;
        return;
    }
    regs[reg] <<= shift;
}

void VM::op_shri() {
    size_t   reg   = fetch_reg();
    uint64_t shift = fetch_u64();
    if (shift >= 64) {
        regs[reg] = regs[reg] < 0 ? -1 : 0;
        return;
    }
    regs[reg] >>= shift;
}