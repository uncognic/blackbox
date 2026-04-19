//
// Created by User on 2026-04-18.
//

#include "ops_control.hpp"
#include "../vm.hpp"
#include <format>

void VM::op_jmp() {
    size_t reg = fetch_reg();
    size_t addr = static_cast<size_t>(regs[reg]);
    if (addr >= prog.code.size()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("JMP address {} out of bounds at pc={}", addr, pc));
    }
    pc = addr;
}

void VM::op_jmpi() {
    uint32_t addr = fetch_u32();
    if (addr >= prog.code.size()) {
        hard_fault(FaultType::OutOfBounds, std::format("JMPI address {} out of bounds at pc={}", addr, pc));
    }
    pc = addr;
}

void VM::op_je() {
    uint32_t addr = fetch_u32();
    if (ZF) {
        if (addr >= prog.code.size()) {
            hard_fault(FaultType::OutOfBounds,
                       std::format("JE address {} out of bounds at pc={}", addr, pc));
        }
        pc = addr;
    }
}

void VM::op_jne() {
    uint32_t addr = fetch_u32();
    if (!ZF) {
        if (addr >= prog.code.size()) {
            hard_fault(FaultType::OutOfBounds,
                       std::format("JNE address {} out of bounds at pc={}", addr, pc));
        }
        pc = addr;
    }
}

void VM::op_jl() {
    uint32_t addr = fetch_u32();
    if (SF != OF) {
        if (addr >= prog.code.size()) {
            hard_fault(FaultType::OutOfBounds,
                       std::format("JL address {} out of bounds at pc={}", addr, pc));
        }
        pc = addr;
    }
}

void VM::op_jge() {
    uint32_t addr = fetch_u32();
    if (SF == OF) {
        if (addr >= prog.code.size()) {
            hard_fault(FaultType::OutOfBounds,
                       std::format("JGE address {} out of bounds at pc={}", addr, pc));
        }
        pc = addr;
    }
}

void VM::op_jb() {
    uint32_t addr = fetch_u32();
    if (CF) {
        if (addr >= prog.code.size()) {
            hard_fault(FaultType::OutOfBounds,
                       std::format("JB address {} out of bounds at pc={}", addr, pc));
        }
        pc = addr;
    }
}

void VM::op_jae() {
    uint32_t addr = fetch_u32();
    if (!CF) {
        if (addr >= prog.code.size()) {
            hard_fault(FaultType::OutOfBounds,
                       std::format("JAE address {} out of bounds at pc={}", addr, pc));
        }
        pc = addr;
    }
}

void VM::op_call() {
    uint32_t addr = fetch_u32();
    uint32_t frame_size = fetch_u32();
    if (addr >= prog.code.size()) {
        hard_fault(FaultType::OutOfBounds, std::format("CALL address {} out of bounds at pc={}", addr, pc));
    }
    push_frame(frame_size, pc);
    pc = addr;
}

void VM::op_ret() {
    pop_frame();
}

void VM::op_halt() {
    uint8_t code = fetch_u8();
    exit_code = static_cast<int>(code);
    halted = true;
}