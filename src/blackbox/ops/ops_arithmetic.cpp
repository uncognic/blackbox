//
// Created by User on 2026-04-18.
//

#include "ops_arithmetic.hpp"
#include "../vm.hpp"
#include <format>

void VM::op_add() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    regs[dst] += regs[src];
}

void VM::op_sub() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    regs[dst] -= regs[src];
}

void VM::op_mul() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    regs[dst] *= regs[src];
}

void VM::op_div() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    if (regs[src] == 0) {
        raise_fault(FaultType::DivZero, std::format("division by zero at pc={}", pc));
    }
    regs[dst] /= regs[src];
}

void VM::op_mod() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    if (regs[src] == 0) {
        raise_fault(FaultType::DivZero, std::format("modulo by zero at pc={}", pc));
    }
    regs[dst] %= regs[src];
}

void VM::op_inc() {
    size_t reg = fetch_reg();
    regs[reg]++;
}

void VM::op_dec() {
    size_t reg = fetch_reg();
    regs[reg]--;
}