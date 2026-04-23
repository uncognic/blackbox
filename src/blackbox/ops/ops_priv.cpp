//
// Created by User on 2026-04-18.
//

#include "ops_priv.hpp"
#include "../vm.hpp"
#include <format>
#include <print>

void VM::op_droppriv() {
    require_privileged("DROPPRIV");
    cur_mode = Mode::Protected;
}

void VM::op_getmode() {
    size_t reg = fetch_reg();
    regs[reg] = (cur_mode == Mode::Protected) ? 0 : 1;
}

void VM::op_regsyscall() {
    require_privileged("REGSYSCALL");

    uint8_t id = fetch_u8();
    uint32_t addr = fetch_u32();

    if (id >= MAX_SYSCALLS) {
        raise_fault(FaultType::BadSyscall,
                    std::format("REGSYSCALL invalid id {} at pc={}", id, pc));
        return;
    }

    syscall_table[id] = addr;
    syscall_registered[id] = true;
}

void VM::op_syscall() {
    if (cur_mode != Mode::Protected) {
        hard_fault(FaultType::Priv,
                   std::format("SYSCALL only allowed in protected mode at pc={}", pc));
    }

    uint8_t id = fetch_u8();

    if (id >= MAX_SYSCALLS) {
        hard_fault(FaultType::BadSyscall, std::format("SYSCALL invalid id {} at pc={}", id, pc));
    }
    if (!syscall_registered[id]) {
        hard_fault(FaultType::BadSyscall,
                   std::format("SYSCALL {} not registered at pc={}", id, pc));
    }

    syscall_return_pc = pc;
    cur_mode = Mode::Privileged;
    pc = syscall_table[id];
}

void VM::op_sysret() {
    require_privileged("SYSRET");
    cur_mode = Mode::Protected;
    pc = syscall_return_pc;
}

void VM::op_regfault() {
    require_privileged("REGFAULT");

    uint8_t fault_id = fetch_u8();
    uint32_t addr = fetch_u32();

    if (fault_id >= FAULT_TABLE_SIZE) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("REGFAULT invalid fault id {} at pc={}", fault_id, pc));
    }

    fault_table[fault_id] = addr;
    fault_registered[fault_id] = true;
}

void VM::op_faultret() {
    require_privileged("FAULTRET");

    if (current_fault == FaultType::Count) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("FAULTRET with no active fault at pc={}", pc));
    }

    current_fault = FaultType::Count;
    cur_mode = Mode::Protected;
    pc = fault_return_pc;
}

void VM::op_getfault() {
    size_t reg = fetch_reg();
    regs[reg] = static_cast<int64_t>(current_fault);
}

void VM::op_setperm() {
    require_privileged("SETPERM");

    uint32_t start = fetch_u32();
    uint32_t count = fetch_u32();
    uint8_t priv_r = fetch_u8();
    uint8_t priv_w = fetch_u8();
    uint8_t prot_r = fetch_u8();
    uint8_t prot_w = fetch_u8();

    for (uint32_t i = 0; i < count; i++) {
        size_t idx = static_cast<size_t>(start) + i;
        if (idx >= op_stack_perms.size()) {
            break;
        }
        op_stack_perms[idx].priv_read = priv_r;
        op_stack_perms[idx].priv_write = priv_w;
        op_stack_perms[idx].prot_read = prot_r;
        op_stack_perms[idx].prot_write = prot_w;
    }
}
