//
// Created by User on 2026-04-18.
//

#include "../vm.hpp"
#include <format>

void VM::op_add() {
    auto& dst = fetch_writable();
    dst += read_operand();
}
void VM::op_sub() {
    auto& dst = fetch_writable();
    dst -= read_operand();
}
void VM::op_mul() {
    auto& dst = fetch_writable();
    dst *= read_operand();
}
void VM::op_div() {
    auto& dst = fetch_writable();
    auto src = read_operand();
    if (src == 0) {
        raise_fault(FaultType::DivZero, std::format("division by zero at pc={}", pc));
    }
    dst /= src;
}
void VM::op_mod() {
    auto& dst = fetch_writable();
    auto src = read_operand();
    if (src == 0) {
        raise_fault(FaultType::DivZero, std::format("modulo by zero at pc={}", pc));
    }
    dst %= src;
}
void VM::op_inc() {
    auto& dst = fetch_writable();
    dst++;
}
void VM::op_dec() {
    auto& dst = fetch_writable();
    dst--;
}