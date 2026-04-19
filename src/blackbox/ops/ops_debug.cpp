//
// Created by User on 2026-04-18.
//

#include "ops_debug.hpp"
#include "../vm.hpp"
#include <format>
#include <print>

void VM::op_break() {
    set_hit_breakpoint();
}

void VM::op_continue() {
    // noop
}

void VM::op_dumpregs() {
    for (size_t i = 0; i < REGISTERS; i++) {
        std::print("R{:02}: {}\n", i, regs[i]);
    }
}

void VM::op_print_stacksize() {
    std::print("{}", op_stack.size());
}