//
// Created by User on 2026-04-18.
//

#include "ops_registers.hpp"
#include "../vm.hpp"
#include <format>

void VM::op_push() {
    operand_push(read_operand());
}

void VM::op_pop() {
    size_t reg = fetch_reg();
    regs[reg]  = operand_pop();
}

void VM::op_cmp() {
    size_t src = fetch_reg();
    size_t dst = fetch_reg();

    int64_t a   = regs[src];
    int64_t b   = regs[dst];
    int64_t res = a - b;

    ZF = (res == 0) ? 1 : 0;
    SF = (res < 0)  ? 1 : 0;
    CF = (static_cast<uint64_t>(a) < static_cast<uint64_t>(b)) ? 1 : 0;

    int64_t overflow = (a ^ b) & (a ^ res);
    OF = (overflow < 0) ? 1 : 0;
    AF = ((a & 0xF) < (b & 0xF)) ? 1 : 0;

    uint8_t pf = static_cast<uint8_t>(res);
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    PF = !(pf & 1);
}