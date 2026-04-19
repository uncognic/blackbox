//
// Created by User on 2026-04-18.
//

#include "ops_memory.hpp"
#include "../vm.hpp"
#include <format>

void VM::op_loadvar() {
    size_t reg = fetch_reg();
    uint32_t slot = fetch_u32();
    regs[reg] = var(slot);
}

void VM::op_storevar() {
    size_t reg = fetch_reg();
    uint32_t slot = fetch_u32();
    var(slot) = regs[reg];
}

void VM::op_loadvar_reg() {
    size_t reg = fetch_reg();
    size_t idx_reg = fetch_reg();
    if (regs[idx_reg] < 0) {
        hard_fault(FaultType::OutOfBounds, std::format("LOADVAR_REG negative slot at pc={}", pc));
    }
    regs[reg] = var(static_cast<uint32_t>(regs[idx_reg]));
}

void VM::op_storevar_reg() {
    size_t reg = fetch_reg();
    size_t idx_reg = fetch_reg();
    if (regs[idx_reg] < 0) {
        hard_fault(FaultType::OutOfBounds, std::format("STOREVAR_REG negative slot at pc={}", pc));
    }
    var(static_cast<uint32_t>(regs[idx_reg])) = regs[reg];
}

void VM::op_loadglobal() {
    size_t reg = fetch_reg();
    uint32_t slot = fetch_u32();
    regs[reg] = global_var(slot);
}

void VM::op_storeglobal() {
    size_t reg = fetch_reg();
    uint32_t slot = fetch_u32();
    global_var(slot) = regs[reg];
}

void VM::op_loadref() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    if (call_stack.size() < 2) {
        hard_fault(FaultType::OutOfBounds, std::format("LOADREF requires a caller frame at pc={}", pc));
    }
    if (regs[src] < 0) {
        hard_fault(FaultType::OutOfBounds, std::format("LOADREF negative slot at pc={}", pc));
    }
    size_t caller_base = call_stack[call_stack.size() - 2].frame_base;
    size_t abs = caller_base + static_cast<size_t>(regs[src]);
    if (abs >= mem_top) {
        hard_fault(FaultType::OutOfBounds, std::format("LOADREF slot {} out of bounds at pc={}", abs, pc));
    }
    regs[dst] = mem[abs];
}

void VM::op_storeref() {
    size_t dst = fetch_reg();
    size_t src = fetch_reg();
    if (call_stack.size() < 2) {
        hard_fault(FaultType::OutOfBounds, std::format("STOREREF requires a caller frame at pc={}", pc));
    }
    if (regs[dst] < 0) {
        hard_fault(FaultType::OutOfBounds, std::format("STOREREF negative slot at pc={}", pc));
    }
    size_t caller_base = call_stack[call_stack.size() - 2].frame_base;
    size_t abs = caller_base + static_cast<size_t>(regs[dst]);
    if (abs >= mem_top) {
        hard_fault(FaultType::OutOfBounds, std::format("STOREREF slot {} out of bounds at pc={}", abs, pc));
    }
    mem[abs] = regs[src];
}

void VM::op_load() {
    size_t reg = fetch_reg();
    uint32_t addr = fetch_u32();
    if (addr >= op_stack.size()) {
        hard_fault(FaultType::OutOfBounds, std::format("LOAD address {} out of bounds at pc={}", addr, pc));
    }
    if (cur_mode == Mode::Privileged && !op_stack_perms[addr].priv_read) {
        raise_fault(FaultType::PermRead,
                    std::format("LOAD read permission denied at slot {} pc={}", addr, pc));
    }
    if (cur_mode == Mode::Protected && !op_stack_perms[addr].prot_read) {
        raise_fault(FaultType::PermRead,
                    std::format("LOAD read permission denied at slot {} pc={}", addr, pc));
    }
    regs[reg] = op_stack[addr];
}

void VM::op_store() {
    size_t reg = fetch_reg();
    uint32_t addr = fetch_u32();
    if (addr >= op_stack.size()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("STORE address {} out of bounds at pc={}", addr, pc));
    }
    if (cur_mode == Mode::Privileged && !op_stack_perms[addr].priv_write) {
        raise_fault(FaultType::PermWrite,
                    std::format("STORE write permission denied at slot {} pc={}", addr, pc));
    }
    if (cur_mode == Mode::Protected && !op_stack_perms[addr].prot_write) {
        raise_fault(FaultType::PermWrite,
                    std::format("STORE write permission denied at slot {} pc={}", addr, pc));
    }
    op_stack[addr] = regs[reg];
}

void VM::op_load_reg() {
    size_t reg = fetch_reg();
    size_t idx_reg = fetch_reg();
    if (regs[idx_reg] < 0) {
        hard_fault(FaultType::OutOfBounds, std::format("LOAD_REG negative address at pc={}", pc));
    }
    uint32_t addr = static_cast<uint32_t>(regs[idx_reg]);
    if (addr >= op_stack.size()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("LOAD_REG address {} out of bounds at pc={}", addr, pc));
    }
    regs[reg] = op_stack[addr];
}

void VM::op_store_reg() {
    size_t reg = fetch_reg();
    size_t idx_reg = fetch_reg();
    if (regs[idx_reg] < 0) {
        hard_fault(FaultType::OutOfBounds, std::format("STORE_REG negative address at pc={}", pc));
    }
    uint32_t addr = static_cast<uint32_t>(regs[idx_reg]);
    if (addr >= op_stack.size()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("STORE_REG address {} out of bounds at pc={}", addr, pc));
    }
    op_stack[addr] = regs[reg];
}

void VM::op_alloc() {
    require_privileged("ALLOC");
    uint32_t elems = fetch_u32();
    if (elems > op_stack.size()) {
        op_stack.resize(elems, 0);
        op_stack_perms.resize(elems, SlotPermission{1, 1, 1, 1});
    }
}

void VM::op_grow() {
    require_privileged("GROW");
    uint32_t elems = fetch_u32();
    if (elems == 0) {
        return;
    }
    size_t new_size = op_stack.size() + elems;
    op_stack.resize(new_size, 0);
    op_stack_perms.resize(new_size, SlotPermission{1, 1, 1, 1});
}

void VM::op_resize() {
    require_privileged("RESIZE");
    uint32_t new_size = fetch_u32();
    op_stack.resize(new_size, 0);
    op_stack_perms.resize(new_size, SlotPermission{1, 1, 1, 1});
}

void VM::op_free() {
    require_privileged("FREE");
    uint32_t elems = fetch_u32();
    if (elems > op_stack.size()) {
        hard_fault(FaultType::OutOfBounds, std::format("FREE {} exceeds op_stack size {} at pc={}", elems,
                                               op_stack.size(), pc));
    }
    size_t new_size = op_stack.size() - elems;
    op_stack.resize(new_size);
    op_stack_perms.resize(new_size);
}