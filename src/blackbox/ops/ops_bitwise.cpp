//
// Created by User on 2026-04-18.
//

#include "ops_bitwise.hpp"
#include "../vm.hpp"
#include <format>

void VM::op_and() {
    auto& dst = fetch_writable();
    dst &= read_operand();
}
void VM::op_or() {
    auto& dst = fetch_writable();
    dst |= read_operand();
}
void VM::op_xor() {
    auto& dst = fetch_writable();
    dst ^= read_operand();
}
void VM::op_not() {
    auto& dst = fetch_writable();
    dst = ~dst;
}
void VM::op_shl() {
    size_t dst = fetch_reg();
    int64_t shift = read_operand();
    if (shift < 0 || shift >= 64) {
        regs[dst] = 0;
        return;
    }
    regs[dst] <<= shift;
}

void VM::op_shr() {
    size_t dst = fetch_reg();
    int64_t shift = read_operand();
    if (shift < 0 || shift >= 64) {
        regs[dst] = regs[dst] < 0 ? -1 : 0;
        return;
    }
    regs[dst] >>= shift;
}
