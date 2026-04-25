#pragma once

#include "../define.hpp"
#include "fault.hpp"
#include "program.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

class VM {
  public:
    explicit VM(Program program, int argc, char** argv);
    int run();
    bool step();

    // debugger
    size_t get_pc() const { return pc; }
    int64_t get_reg(size_t r) const { return regs[r]; }
    size_t get_mem_top() const { return mem_top; }
    size_t get_call_depth() const { return call_stack.size(); }
    bool is_halted() const { return halted; }
    int get_exit_code() const { return exit_code; }
    bool hit_breakpoint() const { return breakpoint; }
    void set_hit_breakpoint() { breakpoint = true; }
    void clear_hit_breakpoint() { breakpoint = false; }
    int64_t read_operand();

  private:
    Program prog;
    size_t pc = 0;
    int exit_code = 0;
    bool halted = false;
    bool breakpoint = false;

    std::array<int64_t, REGISTERS> regs{};

    uint8_t ZF = 0, CF = 0, SF = 0, OF = 0, AF = 0, PF = 0;

    std::vector<int64_t> mem;
    size_t mem_top = 0;
    size_t global_end = 0;

    struct Frame {
        size_t ret_pc;
        size_t frame_base;
    };
    std::vector<Frame> call_stack;

    std::vector<int64_t> op_stack;

    std::vector<SlotPermission> op_stack_perms;

    Mode cur_mode = Mode::Privileged;

    // fd
    struct FD {
        enum class Kind : uint8_t { Closed, StdIn, StdOut, StdErr, File };
        Kind kind = Kind::Closed;
        std::unique_ptr<std::fstream> file;

        std::istream* reader();
        std::ostream* writer();
    };
    std::array<FD, FILE_DESCRIPTORS> fds;

    std::array<size_t, MAX_SYSCALLS> syscall_table{};
    std::array<bool, MAX_SYSCALLS> syscall_registered{};

    static constexpr size_t FAULT_TABLE_SIZE = static_cast<size_t>(FaultType::Count);
    std::array<size_t, FAULT_TABLE_SIZE> fault_table{};
    std::array<bool, FAULT_TABLE_SIZE> fault_registered{};

    FaultType current_fault = FaultType::Count;
    size_t fault_return_pc = 0;
    size_t syscall_return_pc = 0;

    int host_argc;
    char** host_argv;

    uint8_t fetch_u8();
    uint16_t fetch_u16();
    uint32_t fetch_u32();
    int32_t fetch_i32();
    uint64_t fetch_u64();
    int64_t fetch_i64();
    size_t fetch_reg();

    int64_t& fetch_writable();

    void expect_bytes(size_t needed);

    int64_t& var(uint32_t slot);
    int64_t& global_var(uint32_t slot);
    int64_t& heap_addr(uint32_t addr);

    void push_frame(size_t frame_size, size_t ret_pc);
    void pop_frame();

    void operand_push(int64_t value);
    int64_t operand_pop();

    [[noreturn]] void hard_fault(FaultType type, std::string_view message);
    void raise_fault(FaultType type, std::string_view message);

    void require_privileged(std::string_view opname);

    using Handler = void (VM::*)();
    static const std::array<Handler, 256> dispatch_table;

    void op_unknown();

    void op_mov();

    // arithmetic
    void op_add();
    void op_sub();
    void op_mul();
    void op_div();
    void op_mod();
    void op_inc();
    void op_dec();

    // bitwise
    void op_and();
    void op_or();
    void op_xor();
    void op_not();
    void op_shl();
    void op_shr();

    // registers
    void op_push();
    void op_pop();
    void op_cmp();

    // control
    void op_jmp();
    void op_je();
    void op_jne();
    void op_jl();
    void op_jge();
    void op_jb();
    void op_jae();
    void op_call();
    void op_ret();
    void op_halt();

    // memory
    void op_loadref();
    void op_storeref();
    void op_alloc();
    void op_grow();
    void op_resize();
    void op_free();

    // strings
    void op_loadstr();
    void op_printstr();
    void op_eprintstr();

    // io
    void op_write();
    void op_print();
    void op_newline();
    void op_printreg();
    void op_eprintreg();
    void op_printchar();
    void op_eprintchar();
    void op_read();
    void op_readstr();
    void op_readchar();
    void op_fopen();
    void op_fclose();
    void op_fread();
    void op_fwrite();
    void op_fseek();

    // system
    void op_exec();
    void op_sleep();
    void op_rand();
    void op_getkey();
    void op_clrscr();
    void op_getarg();
    void op_getargc();
    void op_getenv();

    // privilege
    void op_syscall();
    void op_sysret();
    void op_droppriv();
    void op_regsyscall();
    void op_setperm();
    void op_getmode();
    void op_regfault();
    void op_faultret();
    void op_getfault();

    // debug
    void op_break();
    void op_continue();
    void op_dumpregs();
    void op_print_stacksize();
};