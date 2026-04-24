//
// Created by User on 2026-04-18.
//

#include "vm.hpp"
#include "fault.hpp"
#include <format>
#include <iostream>
#include <print>

const std::array<VM::Handler, 256> VM::dispatch_table = [] {
    std::array<VM::Handler, 256> t{};
    t.fill(&VM::op_unknown);

    // match opcode to its function
    t[opcode_to_byte(Opcode::ADD)] = &VM::op_add;
    t[opcode_to_byte(Opcode::SUB)] = &VM::op_sub;
    t[opcode_to_byte(Opcode::MUL)] = &VM::op_mul;
    t[opcode_to_byte(Opcode::DIV)] = &VM::op_div;
    t[opcode_to_byte(Opcode::MOD)] = &VM::op_mod;
    t[opcode_to_byte(Opcode::INC)] = &VM::op_inc;
    t[opcode_to_byte(Opcode::DEC)] = &VM::op_dec;

    t[opcode_to_byte(Opcode::AND)] = &VM::op_and;
    t[opcode_to_byte(Opcode::OR)] = &VM::op_or;
    t[opcode_to_byte(Opcode::XOR)] = &VM::op_xor;
    t[opcode_to_byte(Opcode::NOT)] = &VM::op_not;
    t[opcode_to_byte(Opcode::SHL)] = &VM::op_shl;
    t[opcode_to_byte(Opcode::SHR)] = &VM::op_shr;
    t[opcode_to_byte(Opcode::SHLI)] = &VM::op_shli;
    t[opcode_to_byte(Opcode::SHRI)] = &VM::op_shri;

    t[opcode_to_byte(Opcode::PUSH_REG)] = &VM::op_push_reg;
    t[opcode_to_byte(Opcode::PUSHI)] = &VM::op_pushi;
    t[opcode_to_byte(Opcode::POP)] = &VM::op_pop;
    t[opcode_to_byte(Opcode::CMP)] = &VM::op_cmp;

    t[opcode_to_byte(Opcode::JMP)] = &VM::op_jmp;
    t[opcode_to_byte(Opcode::JMPI)] = &VM::op_jmpi;
    t[opcode_to_byte(Opcode::JE)] = &VM::op_je;
    t[opcode_to_byte(Opcode::JNE)] = &VM::op_jne;
    t[opcode_to_byte(Opcode::JL)] = &VM::op_jl;
    t[opcode_to_byte(Opcode::JGE)] = &VM::op_jge;
    t[opcode_to_byte(Opcode::JB)] = &VM::op_jb;
    t[opcode_to_byte(Opcode::JAE)] = &VM::op_jae;
    t[opcode_to_byte(Opcode::CALL)] = &VM::op_call;
    t[opcode_to_byte(Opcode::RET)] = &VM::op_ret;
    t[opcode_to_byte(Opcode::HALT)] = &VM::op_halt;

    t[opcode_to_byte(Opcode::LOAD)] = &VM::op_load;
    t[opcode_to_byte(Opcode::LOAD_REG)] = &VM::op_load_reg;
    t[opcode_to_byte(Opcode::STORE)] = &VM::op_store;
    t[opcode_to_byte(Opcode::STORE_REG)] = &VM::op_store_reg;
    t[opcode_to_byte(Opcode::LOADREF)] = &VM::op_loadref;
    t[opcode_to_byte(Opcode::STOREREF)] = &VM::op_storeref;
    t[opcode_to_byte(Opcode::ALLOC)] = &VM::op_alloc;
    t[opcode_to_byte(Opcode::GROW)] = &VM::op_grow;
    t[opcode_to_byte(Opcode::RESIZE)] = &VM::op_resize;
    t[opcode_to_byte(Opcode::FREE)] = &VM::op_free;

    t[opcode_to_byte(Opcode::LOADSTR)] = &VM::op_loadstr;
    t[opcode_to_byte(Opcode::PRINTSTR)] = &VM::op_printstr;
    t[opcode_to_byte(Opcode::EPRINTSTR)] = &VM::op_eprintstr;

    t[opcode_to_byte(Opcode::WRITE)] = &VM::op_write;
    t[opcode_to_byte(Opcode::PRINT)] = &VM::op_print;
    t[opcode_to_byte(Opcode::NEWLINE)] = &VM::op_newline;
    t[opcode_to_byte(Opcode::PRINTREG)] = &VM::op_printreg;
    t[opcode_to_byte(Opcode::EPRINTREG)] = &VM::op_eprintreg;
    t[opcode_to_byte(Opcode::PRINTCHAR)] = &VM::op_printchar;
    t[opcode_to_byte(Opcode::EPRINTCHAR)] = &VM::op_eprintchar;
    t[opcode_to_byte(Opcode::READ)] = &VM::op_read;
    t[opcode_to_byte(Opcode::READSTR)] = &VM::op_readstr;
    t[opcode_to_byte(Opcode::READCHAR)] = &VM::op_readchar;
    t[opcode_to_byte(Opcode::FOPEN)] = &VM::op_fopen;
    t[opcode_to_byte(Opcode::FCLOSE)] = &VM::op_fclose;
    t[opcode_to_byte(Opcode::FREAD)] = &VM::op_fread;
    t[opcode_to_byte(Opcode::FWRITE_REG)] = &VM::op_fwrite_reg;
    t[opcode_to_byte(Opcode::FWRITE_IMM)] = &VM::op_fwrite_imm;
    t[opcode_to_byte(Opcode::FSEEK_REG)] = &VM::op_fseek_reg;
    t[opcode_to_byte(Opcode::FSEEK_IMM)] = &VM::op_fseek_imm;

    t[opcode_to_byte(Opcode::EXEC)] = &VM::op_exec;
    t[opcode_to_byte(Opcode::SLEEP)] = &VM::op_sleep;
    t[opcode_to_byte(Opcode::SLEEP_REG)] = &VM::op_sleep_reg;
    t[opcode_to_byte(Opcode::RAND)] = &VM::op_rand;
    t[opcode_to_byte(Opcode::GETKEY)] = &VM::op_getkey;
    t[opcode_to_byte(Opcode::CLRSCR)] = &VM::op_clrscr;
    t[opcode_to_byte(Opcode::GETARG)] = &VM::op_getarg;
    t[opcode_to_byte(Opcode::GETARGC)] = &VM::op_getargc;
    t[opcode_to_byte(Opcode::GETENV)] = &VM::op_getenv;

    t[opcode_to_byte(Opcode::SYSCALL)] = &VM::op_syscall;
    t[opcode_to_byte(Opcode::SYSRET)] = &VM::op_sysret;
    t[opcode_to_byte(Opcode::DROPPRIV)] = &VM::op_droppriv;
    t[opcode_to_byte(Opcode::REGSYSCALL)] = &VM::op_regsyscall;
    t[opcode_to_byte(Opcode::SETPERM)] = &VM::op_setperm;
    t[opcode_to_byte(Opcode::GETMODE)] = &VM::op_getmode;
    t[opcode_to_byte(Opcode::REGFAULT)] = &VM::op_regfault;
    t[opcode_to_byte(Opcode::FAULTRET)] = &VM::op_faultret;
    t[opcode_to_byte(Opcode::GETFAULT)] = &VM::op_getfault;

    t[opcode_to_byte(Opcode::BREAK)] = &VM::op_break;
    t[opcode_to_byte(Opcode::CONTINUE)] = &VM::op_continue;
    t[opcode_to_byte(Opcode::DUMPREGS)] = &VM::op_dumpregs;
    t[opcode_to_byte(Opcode::PRINT_STACKSIZE)] = &VM::op_print_stacksize;

    t[opcode_to_byte(Opcode::MOV)] = &VM::op_mov;

    return t;
}();

int64_t VM::read_operand() {
    auto type = static_cast<OperandType>(fetch_u8());
    switch (type) {
        case OperandType::Reg:
            return regs[fetch_reg()];
        case OperandType::Imm:
            return static_cast<int64_t>(fetch_i32());
        case OperandType::Imm64:
            return fetch_i64();
        case OperandType::Bss: {
            uint32_t slot = fetch_u32();
            return global_var(slot);
        }
        case OperandType::BssRef: {
            uint32_t slot = fetch_u32();
            return static_cast<int64_t>(slot);
        }
        case OperandType::Var: {
            uint32_t slot = fetch_u32();
            return var(slot);
        }
        case OperandType::Data: {
            uint32_t idx = fetch_u32();
            if (idx >= prog.data_string_handles.size()) {
                hard_fault(FaultType::OutOfBounds,
                           std::format("DATA index {} out of bounds at pc={}", idx, pc));
            }
            return static_cast<int64_t>(prog.data_string_handles[idx]);
        }
        default:
            hard_fault(FaultType::OutOfBounds, std::format("unknown operand type 0x{:02X} at pc={}",
                                                           static_cast<uint8_t>(type), pc));
    }
}


VM::VM(Program program, int argc, char** argv)
    : prog(std::move(program)), host_argc(argc), host_argv(argv) {
    // set up global memory segment
    global_end = prog.bss_count;
    mem.resize(global_end, 0);
    mem_top = global_end;

    // stdio fds
    fds[0].kind = FD::Kind::StdIn;
    fds[1].kind = FD::Kind::StdOut;
    fds[2].kind = FD::Kind::StdErr;

    pc = prog.entry_point;
}

int VM::run() {
    while (step()) {
    }
    return exit_code;
}

// readers
uint8_t VM::fetch_u8() {
    if (pc >= prog.code.size()) {
        hard_fault(FaultType::OutOfBounds, std::format("fetch_u8: pc={} out of bounds", pc));
    }
    return prog.code[pc++];
}

uint32_t VM::fetch_u32() {
    if (pc + 4 > prog.code.size()) {
        hard_fault(FaultType::OutOfBounds, std::format("fetch_u32: pc={} out of bounds", pc));
    }
    uint32_t v = static_cast<uint32_t>(prog.code[pc]) |
                 (static_cast<uint32_t>(prog.code[pc + 1]) << 8) |
                 (static_cast<uint32_t>(prog.code[pc + 2]) << 16) |
                 (static_cast<uint32_t>(prog.code[pc + 3]) << 24);
    pc += 4;
    return v;
}

int32_t VM::fetch_i32() {
    return static_cast<int32_t>(fetch_u32());
}

uint64_t VM::fetch_u64() {
    uint64_t lo = fetch_u32();
    uint64_t hi = fetch_u32();
    return lo | (hi << 32);
}

int64_t VM::fetch_i64() {
    return static_cast<int64_t>(fetch_u64());
}

uint16_t VM::fetch_u16() {
    if (pc + 2 > prog.code.size()) {
        hard_fault(FaultType::OutOfBounds, std::format("fetch_u16: pc={} out of bounds", pc));
    }
    uint16_t v =
        static_cast<uint16_t>(prog.code[pc]) | (static_cast<uint16_t>(prog.code[pc + 1]) << 8);
    pc += 2;
    return v;
}

size_t VM::fetch_reg() {
    uint8_t r = fetch_u8();
    if (r >= REGISTERS) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("invalid register R{:02} at pc={}", r, pc - 1));
    }
    return static_cast<size_t>(r);
}

// memory helpers 7
int64_t& VM::var(uint32_t slot) {
    if (call_stack.empty()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("LOADVAR/STOREVAR outside any frame at pc={}", pc));
    }
    size_t abs = call_stack.back().frame_base + slot;
    if (abs >= mem_top) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("var slot {} out of bounds (abs={}, mem_top={}) at pc={}", slot, abs,
                               mem_top, pc));
    }
    return mem[abs];
}

int64_t& VM::global_var(uint32_t slot) {
    if (static_cast<size_t>(slot) >= global_end) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("global slot {} out of bounds (global_end={}) at pc={}", slot,
                               global_end, pc));
    }
    return mem[slot];
}

// frames
void VM::push_frame(size_t frame_size, size_t ret_pc) {
    call_stack.push_back(Frame{.ret_pc = ret_pc, .frame_base = mem_top});
    size_t new_top = mem_top + frame_size;
    if (new_top > mem.size()) {
        mem.resize(new_top, 0);
    }
    mem_top = new_top;
}

void VM::pop_frame() {
    if (call_stack.empty()) {
        hard_fault(FaultType::OutOfBounds, "pop_frame: call stack underflow");
    }
    Frame f = call_stack.back();
    call_stack.pop_back();
    mem_top = f.frame_base;
    pc = f.ret_pc;
}

// operand stack
void VM::operand_push(int64_t value) {
    op_stack.push_back(value);
    if (op_stack.size() > op_stack_perms.size()) {
        op_stack_perms.push_back(SlotPermission{1, 1, 1, 1});
    }
}

int64_t VM::operand_pop() {
    if (op_stack.empty()) {
        hard_fault(FaultType::OutOfBounds, std::format("op_pop: stack underflow at pc={}", pc));
    }
    int64_t v = op_stack.back();
    op_stack.pop_back();
    return v;
}

// fault handling
void VM::hard_fault(FaultType type, std::string_view message) {
    throw VMFault{type, std::string(message), pc};
}

void VM::raise_fault(FaultType type, std::string_view message) {
    hard_fault(type, message);
}

void VM::require_privileged(std::string_view opname) {
    if (cur_mode != Mode::Privileged) {
        raise_fault(FaultType::Priv,
                    std::format("{} requires privileged mode at pc={}", opname, pc));
    }
}

// fd
std::istream* VM::FD::reader() {
    switch (kind) {
        case Kind::StdIn:
            return &std::cin;
        case Kind::File:
            return file.get();
        default:
            return nullptr;
    }
}

std::ostream* VM::FD::writer() {
    switch (kind) {
        case Kind::StdOut:
            return &std::cout;
        case Kind::StdErr:
            return &std::cerr;
        case Kind::File:
            return file.get();
        default:
            return nullptr;
    }
}

void VM::op_unknown() {
    hard_fault(FaultType::OutOfBounds,
               std::format("unknown opcode 0x{:02X} at pc={}", prog.code[pc - 1], pc - 1));
}

bool VM::step() {
    if (halted || pc >= prog.code.size()) {
        return false;
    }

    uint8_t byte = prog.code[pc++];

    try {
        (this->*dispatch_table[byte])();
    } catch (const VMFault& f) {
        size_t fault_idx = static_cast<size_t>(f.type);
        if (fault_idx < FAULT_TABLE_SIZE && fault_registered[fault_idx]) {
            current_fault = f.type;
            fault_return_pc = pc;
            cur_mode = Mode::Privileged;
            pc = fault_table[fault_idx];
        } else {
            std::println(stderr, "FAULT [{}] at pc={}: {}", fault_name(f.type), f.program_counter,
                         f.name);
            halted = true;
            exit_code = 1;
        }
    }
    return !halted;
}