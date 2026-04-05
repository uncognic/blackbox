#include "../define.h"
#include "../blackboxc/tools.h"
#include "../data.h"
#include "fmt.h"
#include <algorithm>
#include <array>

#include <charconv>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <fstream>

#include <iostream>
#include <memory>
#include <print>
#include <system_error>
#include <vector>
#include <string_view>
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif
#include "debug.h"

using size_t = decltype(sizeof(0));

static std::string_view trim_left(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    return text;
}

static bool parse_int(std::string_view text, int &value)
{
    int parsed = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size())
        return false;
    value = parsed;
    return true;
}

template <typename T>
static bool ensure_capacity(std::vector<T> &buf, size_t needed)
{
    if (needed <= buf.size())
        return false;

    try
    {
        size_t new_cap = buf.size() ? (buf.size() * 2) : 256;
        while (new_cap < needed)
            new_cap *= 2;
        buf.resize(new_cap);
    }
    catch (...)
    {
        return true;
    }

    return false;
}



int main(int argc, char *argv[])
{
    const char *prog_path = NULL;
    bool debug = false;
    if (argc < 2)
    {
        std::println("Usage: bbx [--debug|-d] program.bcx");
        return 1;
    }
    for (int i = 1; i < argc; i++)
    {
        std::string_view arg = argv[i];
        if (arg == "-d" || arg == "--debug")
            debug = true;
        else if (!prog_path)
            prog_path = argv[i];
    }
    if (!prog_path)
    {
        std::println("Usage: bbx [--debug|-d] program.bcx");
        return 1;
    }

    int64_t registers[REGISTERS] = {0};
    uint8_t ZF = 0;
    uint8_t CF = 0;
    uint8_t SF = 0;
    uint8_t OF = 0;
    uint8_t AF = 0;
    uint8_t PF = 0;
    bool dbg_instructions_shown = false;

    std::ifstream input(prog_path, std::ios::binary);
    if (!input)
    {
        std::println(stderr, "Error opening file: {}", prog_path);
        return 1;
    }

    input.seekg(0, std::ios::end);
    std::streamsize file_size = input.tellg();
    if (file_size < 0)
    {
        std::println(stderr, "Error reading file size: {}", prog_path);
        return 1;
    }
    input.seekg(0, std::ios::beg);

    size_t size = static_cast<size_t>(file_size);

    std::vector<uint8_t> program(size);
    if (!input.read(reinterpret_cast<char *>(program.data()), file_size))
    {
        std::println(stderr, "Error reading file: {}", prog_path);
        return 1;
    }

    if (program.size() < 3)
    {
        std::println(stderr, "Error: program too small (missing magic)");
        return 1;
    }
    uint8_t m0 = (MAGIC >> 16) & 0xFF;
    uint8_t m1 = (MAGIC >> 8) & 0xFF;
    uint8_t m2 = (MAGIC)&0xFF;
    if (program[0] != m0 || program[1] != m1 || program[2] != m2)
    {
        std::println(stderr, "Error: invalid magic (expected '{}{}{}')",
                     static_cast<char>(m0), static_cast<char>(m1), static_cast<char>(m2));
        return 1;
    }

    if (program.size() < HEADER_FIXED_SIZE)
    {
        std::println(stderr, "Error: program too small (missing data table header)");
        return 1;
    }
    uint8_t data_count = program[MAGIC_SIZE];
    uint32_t data_table_size = static_cast<uint32_t>(program[MAGIC_SIZE + 1]) |
                               (static_cast<uint32_t>(program[MAGIC_SIZE + 2]) << 8) |
                               (static_cast<uint32_t>(program[MAGIC_SIZE + 3]) << 16) |
                               (static_cast<uint32_t>(program[MAGIC_SIZE + 4]) << 24);
    if (program.size() < HEADER_FIXED_SIZE + data_table_size)
    {
        std::println(stderr, "Error: program too small for declared data table");
        return 1;
    }
    uint8_t *data_table = program.data() + HEADER_FIXED_SIZE;

    size_t sp = 0;
    size_t stack_cap = STACK_SIZE;
    std::vector<int64_t> stack(stack_cap);

    size_t csp = 0;
    size_t call_stack_cap = 1024;
    std::vector<size_t> call_stack(call_stack_cap);

    size_t vars_sp = 0;
    size_t vars_cap = VAR_CAPACITY;
    std::vector<int64_t> vars(vars_cap);

    size_t fsp = 0;
    size_t frame_stack_cap = 1024;
    std::vector<size_t> frame_base_stack(frame_stack_cap);

    size_t pc = HEADER_FIXED_SIZE + data_table_size;

    size_t str_heap_size = 0;
    std::vector<uint8_t> str_heap(256);

    struct FileSlot
    {
        std::unique_ptr<std::fstream> owned;
        std::istream *in = nullptr;
        std::ostream *out = nullptr;
    };

    std::array<FileSlot, FILE_DESCRIPTORS> fds{};
    auto close_fd = [&](uint8_t fd)
    {
        fds[fd].owned.reset();
        fds[fd].in = nullptr;
        fds[fd].out = nullptr;
    };

    fds[0].in = &std::cin;
    fds[1].out = &std::cout;
    fds[2].out = &std::cerr;

    // permissions
    Mode cur_mode = MODE_PRIVILEGED;
    std::array<uint32_t, MAX_SYSCALLS> syscall_table{};
    std::array<bool, MAX_SYSCALLS> syscall_registered{};
    std::vector<SlotPermission> permissions(stack_cap);
    for (size_t i = 0; i < stack_cap; i++)
    {
        permissions[i].priv_read = 1;
        permissions[i].priv_write = 1;
        permissions[i].prot_read = 1;
        permissions[i].prot_write = 1;
    }

    size_t syscall_return_pc = 0;

    // faults
    uint32_t fault_table[FAULT_COUNT];
    bool fault_registered[FAULT_COUNT];
    Fault current_fault = FAULT_COUNT; // no fault
    size_t fault_return_pc = 0;
    std::fill_n(fault_table, FAULT_COUNT, 0u);
    std::fill_n(fault_registered, FAULT_COUNT, false);

    bool breakpoints_enabled = false;
    bool debug_step = false;
    if (debug)
    {
        std::print("Debugger mode: (s)tep or (b)reakpoint? ");
        fflush(stdout);
        char modebuf[16];
        if (fgets(modebuf, sizeof(modebuf), stdin))
        {
            if (modebuf[0] == 's' || modebuf[0] == 'S' || modebuf[0] == '\n')
            {
                debug_step = true;
            }
            else if (modebuf[0] == 'b' || modebuf[0] == 'B')
            {
                breakpoints_enabled = true;
            }
            else
            {
                debug_step = true;
            }
        }
        else
        {
            debug_step = true;
        }
        debug = debug_step;
        dbg_instructions_shown = false;
    }

#define REQUIRE_PRIVILEGED(name)                                                     \
    if (cur_mode != MODE_PRIVILEGED)                                                 \
    {                                                                                \
        RAISE_FAULT(FAULT_PRIV, name " requires PRIVILEGED mode at pc=%zu", pc - 1); \
        break;                                                                       \
    }

#define RAISE_FAULT(type, msg, ...)                             \
    do                                                          \
    {                                                           \
        if (fault_registered[type])                             \
        {                                                       \
            current_fault = type;                               \
            fault_return_pc = pc;                               \
            cur_mode = MODE_PRIVILEGED;                         \
            pc = fault_table[type];                             \
        }                                                       \
        else                                                    \
        {                                                       \
            bbxc::fmt::err_fmt( "FAULT: " msg "\n", ##__VA_ARGS__); \
            goto fault_exit;                                    \
        }                                                       \
    } while (0)

    while (pc < size)
    {
        if (debug && debug_step)
        {
            uint8_t nxt = program[pc];
            std::println("[DEBUG] pc={} opcode=0x{:02X} {}", pc, nxt, opcode_name(nxt));
            if (!dbg_instructions_shown)
            {
                std::println("Debugger commands:");
                std::println("  Enter - step one instruction");
                std::println("  c - continue (disable debugger)");
                std::println("  q - quit interpreter");
                std::println("  rN - show first N registers (e.g. r20)");
                std::println("  s - show top 8 stack entries");
                std::println("  sN - show top N stack entries (e.g. s10)");
                std::println("  sM-N - show stack entries from index M to N (inclusive)");
                dbg_instructions_shown = true;
            }
            std::print("> ");
            fflush(stdout);
            char cmd[128];
            if (fgets(cmd, sizeof(cmd), stdin))
            {
                std::string_view p = trim_left(cmd);
                if (!p.empty() && p[0] == 'c')
                {
                    debug = false;
                }
                else if (!p.empty() && p[0] == 'q')
                {
                    return 0;
                }
                else if (!p.empty() && p[0] == 'r')
                {
                    int n = 0;
                    if (p.size() > 1)
                        parse_int(p.substr(1), n);
                    if (n <= 0)
                        n = 16;
                    print_regs(registers, n);
                    continue;
                }
                else if (!p.empty() && p[0] == 's')
                {
                    std::string_view q = trim_left(p.substr(1));
                    if (q.empty() || q == "\n")
                    {
                        print_stack(stack.data(), sp);
                        continue;
                    }
                    size_t dash = q.find('-');
                    if (dash != std::string_view::npos)
                    {
                        int a = 0;
                        int b = 0;
                        parse_int(q.substr(0, dash), a);
                        parse_int(q.substr(dash + 1), b);
                        if (a < 0)
                            a = 0;
                        if (b < 0)
                            b = 0;
                        if ((size_t)a >= sp || (size_t)b >= sp || a > b)
                        {
                            std::println("Invalid stack range {}-{} (stack size={})", a, b, sp);
                            continue;
                        }
                        std::println("Stack entries {}..{}:", a, b);
                        for (int i = a; i <= b; i++)
                            std::print(" [{}]={}", i, (long long)stack[i]);
                        std::println("");
                        continue;
                    }
                    if (!q.empty() && q[0] >= '0' && q[0] <= '9')
                    {
                        int n = 0;
                        parse_int(q, n);
                        if (n <= 0)
                            n = 8;
                        size_t show = (size_t)n < sp ? (size_t)n : sp;
                        std::println("Stack size={}, top {} entries:", sp, show);
                        for (size_t i = 0; i < show; i++)
                        {
                            size_t idx = (sp == 0) ? 0 : sp - 1 - i;
                            std::print(" [{}]={}", idx, (long long)stack[idx]);
                        }
                        std::println("");
                        continue;
                    }
                }
            }
        }
        uint8_t opcode = program[pc++];
        switch (opcode)
        {
        case OPCODE_WRITE:
        {
            uint8_t fd = program[pc++];
            uint8_t len = program[pc++];
            if (fd != 1 && fd != 2)
            {
                bbxc::fmt::err_fmt( "Error: invalid fd %hhu at pc=%zu\n", fd, pc);
                return 1;
            }
            if (pc + len > size)
            {
                bbxc::fmt::err_fmt( "Error: string past end of program at pc=%zu\n", pc);
                return 1;
            }
            FILE *out = (fd == 1) ? stdout : stderr;
            size_t written = fwrite(&program[pc], 1, len, out);
            fflush(out);
            if (written != len)
            {
                bbxc::fmt::err_errno("fwrite");
                return 1;
            }
            pc += len;
            break;
        }
        case OPCODE_INC:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for INC at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in INC at pc=%zu\n", pc);
                return 1;
            }
            registers[reg] += 1;
            break;
        }
        case OPCODE_DEC:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for DEC at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in DEC at pc=%zu\n", pc);
                return 1;
            }
            registers[reg] -= 1;
            break;
        }
        case OPCODE_PUSHI:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for PUSH at pc=%zu\n", pc);
                return 1;
            }
            if (sp >= stack_cap)
            {
                size_t new_cap = stack_cap + stack_cap / 2;
                if (new_cap <= sp)
                    new_cap = sp + 1;
                stack.resize(new_cap);
                stack_cap = new_cap;
            }
            int32_t value = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;

            stack[sp++] = value;
            break;
        }
        case OPCODE_PUSH_REG:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for PUSH_REG at pc=%zu\n", pc);
                return 1;
            }
            uint8_t src = program[pc++];
            if (src >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in PUSH_REG at pc=%zu\n", pc);
                return 1;
            }
            if (sp >= stack_cap)
            {
                size_t new_cap = stack_cap + stack_cap / 2;
                if (new_cap <= sp)
                    new_cap = sp + 1;
                stack.resize(new_cap);
                stack_cap = new_cap;
            }
            stack[sp++] = registers[src];
            break;
        }
        case OPCODE_CMP:
        {
            if (pc + 2 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for CMP at pc=%zu\n", pc);
                return 1;
            }
            uint8_t src = program[pc++];
            uint8_t dst = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in CMP at pc=%zu\n", pc);
                return 1;
            }

            int64_t a = registers[src];
            int64_t b = registers[dst];
            int64_t res = a - b;

            if (res == 0)
            {
                ZF = 1;
            }
            else
            {
                ZF = 0;
            }

            if (res < 0)
            {
                SF = 1;
            }
            else
            {
                SF = 0;
            }

            if ((uint64_t)a < (uint64_t)b)
            {
                CF = 1;
            }
            else
            {
                CF = 0;
            }

            int64_t x = (a ^ b) & (a ^ res);
            if (x < 0)
            {
                OF = 1;
            }
            else
            {
                OF = 0;
            }

            if ((a & 0xF) < (b & 0xF))
            {
                AF = 1;
            }
            else
            {
                AF = 0;
            }

            uint8_t v = (uint8_t)res;
            v ^= v >> 4;
            v ^= v >> 2;
            v ^= v >> 1;
            PF = !(v & 1);

            break;
        }
        case OPCODE_POP:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for POP at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register %u at pc=%zu\n", reg, pc);
                return 1;
            }
            if (sp == 0)
            {
                bbxc::fmt::err_fmt( "Stack underflow at pc=%zu\n", pc);
                return 1;
            }
            registers[reg] = stack[--sp];
            break;
        }
        case OPCODE_JE:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for JE at pc=%zu\n", pc);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (ZF)
            {
                if (addr >= size)
                {
                    bbxc::fmt::err_fmt( "JE address out of bounds: %u at pc=%zu\n", addr, pc);
                    return 1;
                }
                pc = addr;
            }
            break;
        }
        case OPCODE_JNE:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for JNE at pc=%zu\n", pc);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (!ZF)
            {
                if (addr >= size)
                {
                    bbxc::fmt::err_fmt( "JNE address out of bounds: %u at pc=%zu\n", addr,
                            pc);
                    return 1;
                }
                pc = addr;
            }
            break;
        }
        case OPCODE_ADD:
        {
            if (pc + 2 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for ADD at pc=%zu at pc=%zu\n", pc,
                        pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in ADD at pc=%zu\n", pc);
                return 1;
            }
            registers[dst] += registers[src];
            break;
        }
        case OPCODE_SUB:
        {
            if (pc + 2 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for SUB at pc=%zu\n", pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in SUB at pc=%zu\n", pc);
                return 1;
            }
            registers[dst] -= registers[src];
            break;
        }
        case OPCODE_MUL:
        {
            if (pc + 2 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for MUL at pc=%zu\n", pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in MUL at pc=%zu\n", pc);
                return 1;
            }
            registers[dst] *= registers[src];
            break;
        }
        case OPCODE_DIV:
        {
            if (pc + 2 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for DIV at pc=%zu\n", pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in DIV at pc=%zu\n", pc);
                return 1;
            }
            if (registers[src] == 0)
            {
                RAISE_FAULT(FAULT_DIV_ZERO, "division by zero at pc=%zu", pc);
                break;
            }
            registers[dst] /= registers[src];
            break;
        }

        case OPCODE_PRINTREG:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for PRINTREG at pc=%zu\n", pc);
                return 1;
            }

            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register");
                return 1;
            }
            std::print("{}", (long long)registers[reg]);
            fflush(stdout);
            break;
        }
        case OPCODE_EPRINTREG:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for EPRINTREG at pc=%zu\n", pc);
                return 1;
            }

            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register");
                return 1;
            }
            std::print(stderr, "{}", (long long)registers[reg]);
            fflush(stderr);
            break;
        }
        case OPCODE_MOV_REG:
        {
            if (pc + 2 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for MOV_REG at pc=%zu\n", pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in MOV_REG at pc=%zu\n", pc);
                return 1;
            }
            registers[dst] = registers[src];
            break;
        }
        case OPCODE_MOVI:
        {
            if (pc + 4 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for MOVI at pc=%zu\n", pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            if (dst >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in MOVI at pc=%zu\n", pc);
                return 1;
            }
            int32_t value = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            registers[dst] = value;
            break;
        }
        case OPCODE_JMP:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for JMP at pc=%zu\n", pc);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc = addr;
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "JMP addr out of bounds: %zu at pc=%u\n", pc, addr);
                return 1;
            }
            break;
        }
        case OPCODE_NEWLINE:
        {
            putchar('\n');
            break;
        }
        case OPCODE_HALT:
        {
            uint8_t code = 0;
            if (pc < size)
            {
                code = program[pc++];
            }
            return (int)code;
        }
        case OPCODE_PRINT:
        {
            if (pc >= size)
            {
                std::println(stderr, "Error: missing operand for PRINT at pc={}", pc);
                return 1;
            }
            uint8_t value = program[pc++];
            putchar(value);
            break;
        }
        case OPCODE_ALLOC:
            REQUIRE_PRIVILEGED("ALLOC");
            {
                if (pc + 3 >= size)
                {
                    bbxc::fmt::err_fmt( "Missing operand for ALLOC at pc=%zu\n", pc);
                    return 1;
                }

                uint32_t elems = program[pc] | (program[pc + 1] << 8) |
                                 (program[pc + 2] << 16) | (program[pc + 3] << 24);
                pc += 4;

                if (elems > stack_cap)
                {
                    size_t old_cap = stack_cap;
                    stack.resize(elems);
                    permissions.resize(elems);
                    for (size_t i = old_cap; i < elems; i++)
                    {
                        permissions[i].priv_read = 1;
                        permissions[i].priv_write = 1;
                        permissions[i].prot_read = 1;
                        permissions[i].prot_write = 1;
                    }
                    stack_cap = elems;
                }
                break;
            }
        case OPCODE_LOAD:
        {
            if (pc + 5 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for LOAD at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in LOAD at pc=%zu\n", pc);
                return 1;
            }
            if ((size_t)addr >= stack_cap)
            {
                RAISE_FAULT(FAULT_OOB, "address out of bounds: %u at pc=%zu", addr, pc);
                break;
            }

            if (cur_mode == MODE_PRIVILEGED && !permissions[addr].priv_read)
            {
                RAISE_FAULT(FAULT_PERM_READ, "read permission denied for %s at slot %u pc=%zu",
                            cur_mode == MODE_PRIVILEGED ? "PRIVILEGED" : "PROTECTED", addr, pc);
                break;
            }
            if (cur_mode == MODE_PROTECTED && !permissions[addr].prot_read)
            {
                RAISE_FAULT(FAULT_PERM_READ, "read permission denied for %s at slot %u pc=%zu",
                            cur_mode == MODE_PRIVILEGED ? "PRIVILEGED" : "PROTECTED", addr, pc);
                break;
            }

            registers[reg] = stack[addr];
            break;
        }
        case OPCODE_LOAD_REG:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for LOAD_REG at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint8_t idxreg = program[pc++];
            if (reg >= REGISTERS || idxreg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in LOAD_REG at pc=%zu\n", pc);
                return 1;
            }
            int64_t idx64 = registers[idxreg];
            if (idx64 < 0 || (size_t)idx64 >= stack_cap)
            {
                bbxc::fmt::err_fmt( "LOAD_REG address out of bounds: %lld at pc=%zu\n",
                        (long long)idx64, pc);
                return 1;
            }

            if (cur_mode == MODE_PRIVILEGED && !permissions[idx64].priv_read)
            {
                RAISE_FAULT(FAULT_PERM_READ, "read permission denied for %s at slot %u pc=%zu",
                            cur_mode == MODE_PRIVILEGED ? "PRIVILEGED" : "PROTECTED", (unsigned int)idx64, pc);
                break;
            }
            if (cur_mode == MODE_PROTECTED && !permissions[idx64].prot_read)
            {
                RAISE_FAULT(FAULT_PERM_READ, "read permission denied for %s at slot %u pc=%zu",
                            cur_mode == MODE_PRIVILEGED ? "PRIVILEGED" : "PROTECTED", (unsigned int)idx64, pc);
                break;
            }

            registers[reg] = stack[(size_t)idx64];
            break;
        }
        case OPCODE_STORE:
        {
            if (pc + 5 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for STORE at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in STORE at pc=%zu\n", pc);
                return 1;
            }
            if ((size_t)addr >= stack_cap)
            {
                RAISE_FAULT(FAULT_OOB, "address out of bounds: %u at pc=%zu", addr, pc);
                break;
            }

            if (cur_mode == MODE_PRIVILEGED && !permissions[addr].priv_write)
            {
                RAISE_FAULT(FAULT_PERM_WRITE, "write permission denied for %s at slot %u pc=%zu",
                            cur_mode == MODE_PRIVILEGED ? "PRIVILEGED" : "PROTECTED", addr, pc);
                break;
            }
            if (cur_mode == MODE_PROTECTED && !permissions[addr].prot_write)
            {
                RAISE_FAULT(FAULT_PERM_WRITE, "write permission denied for %s at slot %u pc=%zu",
                            cur_mode == MODE_PRIVILEGED ? "PRIVILEGED" : "PROTECTED", addr, pc);
                break;
            }

            stack[addr] = registers[reg];
            break;
        }
        case OPCODE_STORE_REG:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for STORE_REG at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint8_t idxreg = program[pc++];
            if (reg >= REGISTERS || idxreg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in STORE_REG at pc=%zu\n", pc);
                return 1;
            }
            int64_t idx64 = registers[idxreg];
            if (idx64 < 0 || (size_t)idx64 >= stack_cap)
            {
                bbxc::fmt::err_fmt( "STORE_REG address out of bounds: %lld at pc=%zu\n",
                        (long long)idx64, pc);
                return 1;
            }

            if (cur_mode == MODE_PRIVILEGED && !permissions[idx64].priv_write)
            {
                RAISE_FAULT(FAULT_PERM_WRITE, "write permission denied for %s at slot %u pc=%zu",
                            cur_mode == MODE_PRIVILEGED ? "PRIVILEGED" : "PROTECTED", (unsigned int)idx64, pc);
                break;
            }
            if (cur_mode == MODE_PROTECTED && !permissions[idx64].prot_write)
            {
                RAISE_FAULT(FAULT_PERM_WRITE, "write permission denied for %s at slot %u pc=%zu",
                            cur_mode == MODE_PRIVILEGED ? "PRIVILEGED" : "PROTECTED", (unsigned int)idx64, pc);
                break;
            }

            stack[(size_t)idx64] = registers[reg];
            break;
        }
        case OPCODE_LOADVAR:
        {
            if (pc + 5 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for LOADVAR at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t slot = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in LOADVAR at pc=%zu\n", pc);
                return 1;
            }
            size_t frame_base = (fsp == 0) ? 0 : frame_base_stack[fsp - 1];
            size_t abs_idx = frame_base + (size_t)slot;
            if (abs_idx >= vars_cap || abs_idx >= vars_sp)
            {
                bbxc::fmt::err_fmt( "LOADVAR slot out of bounds: %zu at pc=%zu\n", abs_idx,
                        pc);
                return 1;
            }
            registers[reg] = vars[abs_idx];
            break;
        }
        case OPCODE_LOADVAR_REG:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for LOADVAR_REG at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint8_t idxreg = program[pc++];
            if (reg >= REGISTERS || idxreg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in LOADVAR_REG at pc=%zu\n", pc);
                return 1;
            }
            int64_t slot64 = registers[idxreg];
            if (slot64 < 0)
            {
                bbxc::fmt::err_fmt( "LOADVAR_REG negative slot: %lld at pc=%zu\n",
                        (long long)slot64, pc);
                return 1;
            }
            size_t frame_base = (fsp == 0) ? 0 : frame_base_stack[fsp - 1];
            size_t abs_idx = frame_base + (size_t)slot64;
            if (abs_idx >= vars_cap || abs_idx >= vars_sp)
            {
                bbxc::fmt::err_fmt( "LOADVAR_REG slot out of bounds: %zu at pc=%zu\n",
                        abs_idx, pc);
                return 1;
            }
            registers[reg] = vars[abs_idx];
            break;
        }
        case OPCODE_STOREVAR:
        {
            if (pc + 5 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for STOREVAR at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t slot = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in STOREVAR at pc=%zu\n", pc);
                return 1;
            }
            size_t frame_base = (fsp == 0) ? 0 : frame_base_stack[fsp - 1];
            size_t abs_idx = frame_base + (size_t)slot;
            if (abs_idx >= vars_cap || abs_idx >= vars_sp)
            {
                bbxc::fmt::err_fmt( "STOREVAR slot out of bounds: %zu at pc=%zu\n", abs_idx,
                        pc);
                return 1;
            }
            vars[abs_idx] = registers[reg];
            break;
        }
        case OPCODE_STOREVAR_REG:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for STOREVAR_REG at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint8_t idxreg = program[pc++];
            if (reg >= REGISTERS || idxreg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in STOREVAR_REG at pc=%zu\n", pc);
                return 1;
            }
            int64_t slot64 = registers[idxreg];
            if (slot64 < 0)
            {
                bbxc::fmt::err_fmt( "STOREVAR_REG negative slot: %lld at pc=%zu\n",
                        (long long)slot64, pc);
                return 1;
            }
            size_t frame_base = (fsp == 0) ? 0 : frame_base_stack[fsp - 1];
            size_t abs_idx = frame_base + (size_t)slot64;
            if (abs_idx >= vars_cap || abs_idx >= vars_sp)
            {
                bbxc::fmt::err_fmt( "STOREVAR_REG slot out of bounds: %zu at pc=%zu\n",
                        abs_idx, pc);
                return 1;
            }
            vars[abs_idx] = registers[reg];
            break;
        }
        case OPCODE_GROW:
            REQUIRE_PRIVILEGED("GROW");
            {
                if (pc + 3 >= size)
                {
                    bbxc::fmt::err_fmt( "Missing operand for GROW at pc=%zu\n", pc);
                    return 1;
                }

                uint32_t elem = program[pc] | (program[pc + 1] << 8) |
                                (program[pc + 2] << 16) | (program[pc + 3] << 24);
                pc += 4;

                if (elem == 0)
                    break;

                size_t new_cap = stack_cap + elem;

                size_t old_cap = stack_cap;
                stack.resize(new_cap);
                permissions.resize(new_cap);
                for (size_t i = old_cap; i < new_cap; i++)
                {
                    permissions[i].priv_read = 1;
                    permissions[i].priv_write = 1;
                    permissions[i].prot_read = 1;
                    permissions[i].prot_write = 1;
                }
                stack_cap = new_cap;
                break;
            }
        case OPCODE_PRINT_STACKSIZE:
        {
            std::print("{}", stack_cap);
            fflush(stdout);
            break;
        }
        case OPCODE_RESIZE:
            REQUIRE_PRIVILEGED("RESIZE");
            {
                if (pc + 3 >= size)
                {
                    bbxc::fmt::err_fmt( "Missing operand for RESIZE at pc=%zu\n", pc);
                    return 1;
                }

                uint32_t new_size = program[pc] | (program[pc + 1] << 8) |
                                    (program[pc + 2] << 16) | (program[pc + 3] << 24);
                pc += 4;

                size_t old_cap = stack_cap;
                stack.resize(new_size);
                permissions.resize(new_size);
                for (size_t i = old_cap; i < new_size; i++)
                {
                    permissions[i].priv_read = 1;
                    permissions[i].priv_write = 1;
                    permissions[i].prot_read = 1;
                    permissions[i].prot_write = 1;
                }
                stack_cap = new_size;
                if (sp > stack_cap)
                    sp = stack_cap;
                break;
            }
        case OPCODE_FREE:
            REQUIRE_PRIVILEGED("FREE");
            {
                if (pc + 3 >= size)
                {
                    bbxc::fmt::err_fmt( "Missing operand for FREE at pc=%zu\n", pc);
                    return 1;
                }

                uint32_t num = program[pc] | (program[pc + 1] << 8) |
                               (program[pc + 2] << 16) | (program[pc + 3] << 24);
                pc += 4;

                if (num == 0)
                    break;

                if (num > stack_cap)
                {
                    bbxc::fmt::err_fmt( "FREE size out of bounds: %u at pc=%zu\n", num, pc);
                    return 1;
                }

                size_t new_cap = stack_cap - num;
                stack.resize(new_cap);
                stack_cap = new_cap;
                break;
            }
        case OPCODE_FOPEN:
            REQUIRE_PRIVILEGED("FOPEN");
            {
                if (pc + 2 >= size)
                {
                    bbxc::fmt::err_fmt( "Missing operands for FOPEN at pc=%zu\n", pc);
                    return 1;
                }
                uint8_t mode_str = program[pc++];
                uint8_t fd = program[pc++];
                uint8_t fname_len = program[pc++];

                if (fd >= FILE_DESCRIPTORS)
                {
                    bbxc::fmt::err_fmt( "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                    return 1;
                }
                if (fname_len == 0 || fname_len >= 255)
                {
                    bbxc::fmt::err_fmt( "Invalid filename length %u at pc=%zu\n", fname_len,
                            pc);
                    return 1;
                }
                if (pc + fname_len > size)
                {
                    bbxc::fmt::err_fmt( "Filename past end of program at pc=%zu\n", pc);
                    return 1;
                }

                char fname[256];
                std::copy_n(&program[pc], fname_len, fname);
                fname[fname_len] = '\0';
                pc += fname_len;

                close_fd(fd);

                if (mode_str > 2)
                {
                    bbxc::fmt::err_fmt( "Invalid mode %u at pc=%zu\n", mode_str, pc);
                    return 1;
                }

                if (std::string_view(fname) == "/dev/stdout")
                {
                    fds[fd].out = &std::cout;
                }
                else if (std::string_view(fname) == "/dev/stderr")
                {
                    fds[fd].out = &std::cerr;
                }
                else if (std::string_view(fname) == "/dev/stdin")
                {
                    fds[fd].in = &std::cin;
                }
                else
                {
                    std::ios::openmode open_mode{};
                    if (mode_str == 0)
                        open_mode = std::ios::in;
                    else if (mode_str == 1)
                        open_mode = std::ios::out | std::ios::trunc;
                    else
                        open_mode = std::ios::out | std::ios::app;

                    auto file = std::make_unique<std::fstream>(fname, open_mode);
                    if (!file->is_open())
                    {
                        bbxc::fmt::err_fmt( "fopen failed for '%s' at pc=%zu\n", fname, pc);
                        return 1;
                    }

                    if (mode_str == 0)
                        fds[fd].in = file.get();
                    else
                        fds[fd].out = file.get();

                    fds[fd].owned = std::move(file);
                }
                break;
            }
        case OPCODE_FCLOSE:
            REQUIRE_PRIVILEGED("FCLOSE");
            {
                if (pc >= size)
                {
                    bbxc::fmt::err_fmt( "Missing operand for FCLOSE at pc=%zu\n", pc);
                    return 1;
                }
                uint8_t fd = program[pc++];
                if (fd >= FILE_DESCRIPTORS)
                {
                    bbxc::fmt::err_fmt( "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                    return 1;
                }
                close_fd(fd);
                break;
            }
        case OPCODE_FREAD:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for FREAD at pc=%zu\n", pc);
                return 1;
            }
            size_t operand_pc = pc;
            uint8_t fd = program[pc++];
            uint8_t reg = program[pc++];
            if (fd >= FILE_DESCRIPTORS)
            {
                bbxc::fmt::err_fmt( "Invalid file descriptor %u at pc=%zu\n", fd,
                        operand_pc);
                return 1;
            }
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register %u at pc=%zu\n", reg, operand_pc);
                return 1;
            }
            if (!fds[fd].in)
            {
                bbxc::fmt::err_fmt( "File descriptor %u not opened at pc=%zu\n", fd,
                        operand_pc);
                return 1;
            }
            int c = fds[fd].in->get();
            if (c == EOF)
            {
                if (fds[fd].in->eof())
                {
                    registers[reg] = -1;
                }
                else
                {
                    bbxc::fmt::err_fmt( "fgetc failed at pc=%zu\n", operand_pc);
                    return 1;
                }
            }
            else
            {
                registers[reg] = (int64_t)c;
            }
            break;
        }
        case OPCODE_FWRITE_REG:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for FWRITE_REG at pc=%zu\n", pc);
                return 1;
            }
            uint8_t fd = program[pc++];
            uint8_t reg = program[pc++];
            if (fd >= FILE_DESCRIPTORS)
            {
                bbxc::fmt::err_fmt( "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                return 1;
            }
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register %u at pc=%zu\n", reg, pc);
                return 1;
            }
            if (!fds[fd].out)
            {
                bbxc::fmt::err_fmt( "File descriptor %u not opened at pc=%zu\n", fd, pc);
                return 1;
            }
            int val = (int)registers[reg];
            fds[fd].out->put(static_cast<char>(val));
            fds[fd].out->flush();
            if (!(*fds[fd].out))
            {
                bbxc::fmt::err_fmt( "fputc failed at pc=%zu\n", pc);
                return 1;
            }
            break;
        }
        case OPCODE_FWRITE_IMM:
        {
            if (pc + 4 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for FWRITE_IMM at pc=%zu\n", pc);
                return 1;
            }
            uint8_t fd = program[pc++];
            int32_t value = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (fd >= FILE_DESCRIPTORS)
            {
                bbxc::fmt::err_fmt( "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                return 1;
            }
            if (!fds[fd].out)
            {
                bbxc::fmt::err_fmt( "File descriptor %u not opened at pc=%zu\n", fd, pc);
                return 1;
            }
            fds[fd].out->put(static_cast<char>(value));
            fds[fd].out->flush();
            if (!(*fds[fd].out))
            {
                bbxc::fmt::err_fmt( "fputc failed at pc=%zu\n", pc);
                return 1;
            }
            break;
        }
        case OPCODE_FSEEK_REG:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for FSEEK_REG at pc=%zu\n", pc);
                return 1;
            }
            uint8_t fd = program[pc++];
            uint8_t reg = program[pc++];
            if (fd >= FILE_DESCRIPTORS)
            {
                bbxc::fmt::err_fmt( "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                return 1;
            }
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register %u at pc=%zu\n", reg, pc);
                return 1;
            }
            if (!fds[fd].in && !fds[fd].out)
            {
                bbxc::fmt::err_fmt( "File descriptor %u not opened at pc=%zu\n", fd, pc);
                return 1;
            }
            std::streamoff pos = static_cast<std::streamoff>(registers[reg]);
            if (fds[fd].in)
            {
                fds[fd].in->clear();
                fds[fd].in->seekg(pos, std::ios::beg);
                if (!(*fds[fd].in))
                {
                    bbxc::fmt::err_fmt( "fseek failed at pc=%zu\n", pc);
                    return 1;
                }
            }
            if (fds[fd].out)
            {
                fds[fd].out->clear();
                fds[fd].out->seekp(pos, std::ios::beg);
                if (!(*fds[fd].out))
                {
                    bbxc::fmt::err_fmt( "fseek failed at pc=%zu\n", pc);
                    return 1;
                }
            }
            break;
        }
        case OPCODE_FSEEK_IMM:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for FSEEK_IMM at pc=%zu\n", pc);
                return 1;
            }
            uint8_t fd = program[pc++];
            int32_t offset = program[pc] | (program[pc + 1] << 8) |
                             (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (fd >= FILE_DESCRIPTORS)
            {
                bbxc::fmt::err_fmt( "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                return 1;
            }
            if (!fds[fd].in && !fds[fd].out)
            {
                bbxc::fmt::err_fmt( "File descriptor %u not opened at pc=%zu\n", fd, pc);
                return 1;
            }
            std::streamoff pos = static_cast<std::streamoff>(offset);
            if (fds[fd].in)
            {
                fds[fd].in->clear();
                fds[fd].in->seekg(pos, std::ios::beg);
                if (!(*fds[fd].in))
                {
                    bbxc::fmt::err_fmt( "fseek failed at pc=%zu\n", pc);
                    return 1;
                }
            }
            if (fds[fd].out)
            {
                fds[fd].out->clear();
                fds[fd].out->seekp(pos, std::ios::beg);
                if (!(*fds[fd].out))
                {
                    bbxc::fmt::err_fmt( "fseek failed at pc=%zu\n", pc);
                    return 1;
                }
            }
            break;
        }
        case OPCODE_LOADSTR:
        {
            if (pc + 4 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for LOADSTR at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t offset = program[pc] | (program[pc + 1] << 8) |
                              (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in LOADSTR at pc=%zu\n", pc);
                return 1;
            }
            if (offset >= data_table_size)
            {
                bbxc::fmt::err_fmt( "Data offset out of bounds: %u at pc=%zu\n", offset,
                        pc);
                return 1;
            }
            registers[reg] = offset;
            break;
        }
        case OPCODE_PRINTSTR:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for PRINTSTR at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in PRINTSTR at pc=%zu\n", pc);
                return 1;
            }

            uint32_t val = (uint32_t)registers[reg];
            if (val & 0x80000000)
            {
                size_t offset = (size_t)(val & 0x7FFFFFFF);
                if (offset >= str_heap_size)
                {
                    bbxc::fmt::err_fmt( "String offset out of bounds: %zu at pc=%zu\n", offset, pc);
                    return 1;
                }
                size_t i = offset;
                while (i < str_heap_size)
                {
                    char c = (char)str_heap[i];
                    if (c == '\0')
                        break;
                    putchar(c);
                    i++;
                }
            }
            else
            {
                if (val >= data_table_size)
                {
                    bbxc::fmt::err_fmt( "Data offset out of bounds: %u at pc=%zu\n", val, pc);
                    return 1;
                }
                bbxc::fmt::out_fmt("%s", &data_table[val]);
            }
            fflush(stdout);
            break;
        }
        case OPCODE_EPRINTSTR:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for EPRINTSTR at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in EPRINTSTR at pc=%zu\n", pc);
                return 1;
            }

            uint32_t val = (uint32_t)registers[reg];
            if (val & 0x80000000)
            {
                size_t offset = (size_t)(val & 0x7FFFFFFF);
                if (offset >= str_heap_size)
                {
                    bbxc::fmt::err_fmt( "String offset out of bounds: %zu at pc=%zu\n", offset, pc);
                    return 1;
                }
                size_t i = offset;
                while (i < str_heap_size)
                {
                    char c = (char)str_heap[i];
                    if (c == '\0')
                        break;
                    fputc(c, stderr);
                    i++;
                }
            }
            else
            {
                if (val >= data_table_size)
                {
                    bbxc::fmt::err_fmt( "Data offset out of bounds: %u at pc=%zu\n", val, pc);
                    return 1;
                }
                bbxc::fmt::err_fmt( "%s", &data_table[val]);
            }
            fflush(stderr);
            break;
        }
        case OPCODE_NOT:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for NOT at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in NOT at pc=%zu\n", pc);
                return 1;
            }
            registers[reg] = ~registers[reg];
            break;
        }
        case OPCODE_AND:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for AND at pc=%zu\n", pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in AND at pc=%zu\n", pc);
                return 1;
            }
            registers[dst] = registers[dst] & registers[src];
            break;
        }
        case OPCODE_OR:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for OR at pc=%zu\n", pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in OR at pc=%zu\n", pc);
                return 1;
            }
            registers[dst] = registers[dst] | registers[src];
            break;
        }
        case OPCODE_XOR:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for XOR at pc=%zu\n", pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in XOR at pc=%zu\n", pc);
                return 1;
            }
            registers[dst] = registers[dst] ^ registers[src];
            break;
        }
        case OPCODE_READSTR:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for READSTR at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in READSTR at pc=%zu\n", pc);
                return 1;
            }

            if (str_heap_size > 0x7FFFFFFF)
            {
                bbxc::fmt::err_fmt( "String heap too large at pc=%zu\n", pc);
                return 1;
            }

            uint32_t start_addr = 0x80000000u | (uint32_t)str_heap_size;

            int c;
            while ((c = getchar()) != EOF && c != '\n')
            {
                if (ensure_capacity(str_heap, str_heap_size + 1))
                {
                    bbxc::fmt::err_errno("realloc");
                    return 1;
                }
                str_heap[str_heap_size++] = (uint8_t)c;
            }
            if (ensure_capacity(str_heap, str_heap_size + 1))
            {
                bbxc::fmt::err_errno("realloc");
                return 1;
            }
            str_heap[str_heap_size++] = 0;

            registers[reg] = start_addr;
            break;
        }
        case OPCODE_SLEEP:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for SLEEP at pc=%zu\n", pc);
                return 1;
            }
            uint32_t ms = program[pc] | (program[pc + 1] << 8) |
                          (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
#ifdef _WIN32
            Sleep((DWORD)ms);
#else
            struct timespec req;
            req.tv_sec = ms / 1000;
            req.tv_nsec = (ms % 1000) * 1000000L;
            nanosleep(&req, NULL);
#endif
            break;
        }
        case OPCODE_SLEEP_REG:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing register operand for SLEEP at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register %u for SLEEP at pc=%zu\n", reg, pc);
                return 1;
            }
            int64_t raw = registers[reg];
            uint64_t ms_val = raw < 0 ? 0 : (uint64_t)raw;
#ifdef _WIN32
            Sleep((DWORD)ms_val);
#else
            struct timespec req2;
            req2.tv_sec = ms_val / 1000;
            req2.tv_nsec = (ms_val % 1000) * 1000000L;
            nanosleep(&req2, NULL);
#endif
            break;
        }
        case OPCODE_CLRSCR:
        {
#ifdef _WIN32
            std::print("\x1b[2J\x1b[H");
            fflush(stdout);
#else
            std::print("\x1b[2J\x1b[H");
            fflush(stdout);
#endif
            break;
        }
        case OPCODE_RAND:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for RAND at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in RAND at pc=%zu\n", pc);
                return 1;
            }
            if (pc + 16 <= size)
            {
                int64_t min = read_i64(program.data(), &pc);
                int64_t max = read_i64(program.data(), &pc);
                uint64_t r = get_true_random();
                if (min > max)
                {
                    int64_t t = min;
                    min = max;
                    max = t;
                }
                uint64_t range;
                if ((uint64_t)(max - min) == UINT64_MAX)
                {
                    registers[reg] = (int64_t)r;
                }
                else
                {
                    range = (uint64_t)(max - min) + 1;
                    if (range == 0)
                        registers[reg] = (int64_t)r;
                    else
                        registers[reg] = min + (int64_t)(r % range);
                }
            }
            else
            {
                registers[reg] = (int64_t)get_true_random();
            }
            break;
        }
        case OPCODE_GETKEY:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for GETKEY at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in GETKEY at pc=%zu\n", pc);
                return 1;
            }
#ifdef _WIN32
            if (_kbhit())
            {
                registers[reg] = (int64_t)_getch();
            }
            else
            {
                registers[reg] = -1;
            }
#else
            {
                struct termios oldt, newt;
                int oldf;

                tcgetattr(STDIN_FILENO, &oldt);
                newt = oldt;
                newt.c_lflag &= ~(ICANON | ECHO);
                tcsetattr(STDIN_FILENO, TCSANOW, &newt);

                oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
                fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

                int ch = getchar();

                tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
                fcntl(STDIN_FILENO, F_SETFL, oldf);

                if (ch == EOF)
                {
                    clearerr(stdin);
                    registers[reg] = -1;
                }
                else
                {
                    registers[reg] = (int64_t)ch;
                }
            }
#endif
            break;
        }
        case OPCODE_READ:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for READ at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in READ at pc=%zu\n", pc);
                return 1;
            }

            long long v;
            if (scanf("%lld", &v) != 1)
                registers[reg] = 0;
            else
                registers[reg] = (int64_t)v;

            // drain remainder of line so next readstr gets a clean buffer
            int c;
            while ((c = getchar()) != EOF && c != '\n')
                ;
            break;
        }
        case OPCODE_READCHAR:
        {
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for READCHAR at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in READCHAR at pc=%zu\n", pc);
                return 1;
            }

            int c;
            while ((c = getchar()) != EOF &&
                   (c == ' ' || c == '\t' || c == '\n' || c == '\r'))
                ;
            if (c == EOF)
            {
                registers[reg] = 0;
            }
            else
            {
                registers[reg] = (int64_t)(unsigned char)c;
                int ch;
                while ((ch = getchar()) != EOF && ch != '\n')
                    ;
            }
            break;
        }
        case OPCODE_BREAK:
        {
            if (breakpoints_enabled)
            {
                std::println("[BREAK] pc={} opcode=0x{:02X} {}", pc - 1,
                       (unsigned int)program[pc - 1], opcode_name(program[pc - 1]));
                if (!dbg_instructions_shown)
                {
                    std::println("Debugger commands:");
                    std::println("Enter - step one instruction");
                    std::println("c - continue (disable debugger)");
                    std::println("q - quit interpreter");
                    std::println("rN - show first N registers (e.g. r20)");
                    std::println("s - show top 8 stack entries");
                    std::println("sN - show top N stack entries (e.g. s10)");
                    std::println("sM-N - show stack entries from index M to N (inclusive)");
                    dbg_instructions_shown = true;
                }
                for (;;)
                {
                    std::print("> ");
                    fflush(stdout);
                    char cmdb[128];
                    if (!fgets(cmdb, sizeof(cmdb), stdin))
                        break;
                    std::string_view p = trim_left(cmdb);
                    if (!p.empty() && p[0] == 'c')
                    {
                        break;
                    }
                    else if (!p.empty() && p[0] == 'q')
                    {
                        return 0;
                    }
                    else if (!p.empty() && p[0] == 'r')
                    {
                        int n = 0;
                        if (p.size() > 1)
                            parse_int(p.substr(1), n);
                        if (n <= 0)
                            n = 16;
                        print_regs(registers, n);
                        continue;
                    }
                    else if (!p.empty() && p[0] == 's')
                    {
                        std::string_view q = trim_left(p.substr(1));
                        if (q.empty() || q == "\n")
                        {
                            print_stack(stack.data(), sp);
                        }
                        else
                        {
                            size_t dash = q.find('-');
                            if (dash != std::string_view::npos)
                            {
                                int a = 0;
                                int b = 0;
                                parse_int(q.substr(0, dash), a);
                                parse_int(q.substr(dash + 1), b);
                                if (a < 0)
                                    a = 0;
                                if (b < 0)
                                    b = 0;
                                if ((size_t)a >= sp || (size_t)b >= sp || a > b)
                                {
                                    std::println("Invalid stack range {}-{} (stack size={})", a, b,
                                           sp);
                                }
                                else
                                {
                                    std::println("Stack entries {}..{}:", a, b);
                                    for (int i = a; i <= b; i++)
                                        std::print(" [{}]={}", i, (long long)stack[i]);
                                    std::println("");
                                }
                            }
                            else if (!q.empty() && q[0] >= '0' && q[0] <= '9')
                            {
                                int n = 0;
                                parse_int(q, n);
                                if (n <= 0)
                                    n = 8;
                                size_t show = (size_t)n < sp ? (size_t)n : sp;
                                std::println("Stack size={}, top {} entries:", sp, show);
                                for (size_t i = 0; i < show; i++)
                                {
                                    size_t idx = (sp == 0) ? 0 : sp - 1 - i;
                                    std::print(" [{}]={}", idx, (long long)stack[idx]);
                                }
                                std::println("");
                            }
                        }
                        continue;
                    }
                    else
                    {
                        continue;
                    }
                }
            }
            else
            {
                break;
            }
            break;
        }
        case OPCODE_CONTINUE:
        {
            break;
        }
        case OPCODE_JL:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for JL at pc=%zu\n", pc);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (SF != OF)
            {
                if (pc >= size)
                {
                    bbxc::fmt::err_fmt( "JL addr out of bounds: %zu at pc=%u\n", pc, addr);
                    return 1;
                }
                pc = addr;
            }
            break;
        }
        case OPCODE_JGE:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for JGE at pc=%zu\n", pc);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (SF == OF)
            {
                if (pc >= size)
                {
                    bbxc::fmt::err_fmt( "JGE addr out of bounds: %zu at pc=%u\n", pc, addr);
                    return 1;
                }
                pc = addr;
            }
            break;
        }
        case OPCODE_JB:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for JB at pc=%zu\n", pc);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (CF == 1)
            {
                if (pc >= size)
                {
                    bbxc::fmt::err_fmt( "JB addr out of bounds: %zu at pc=%u\n", pc, addr);
                    return 1;
                }
                pc = addr;
            }
            break;
        }
        case OPCODE_JAE:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for JAE at pc=%zu\n", pc);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (CF == 0)
            {
                if (pc >= size)
                {
                    bbxc::fmt::err_fmt( "JAE addr out of bounds: %zu at pc=%u\n", pc, addr);
                    return 1;
                }
                pc = addr;
            }
            break;
        }
        case OPCODE_CALL:
        {
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for CALL at pc=%zu\n", pc);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (pc + 3 >= size)
            {
                bbxc::fmt::err_fmt( "Missing frame size for CALL at pc=%zu\n", pc);
                return 1;
            }
            uint32_t frame_size = program[pc] | (program[pc + 1] << 8) |
                                  (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (addr >= size)
            {
                bbxc::fmt::err_fmt( "CALL addr out of bounds: %zu at pc=%u\n", pc, addr);
                return 1;
            }
            if (csp >= call_stack_cap)
            {
                size_t new_cap = call_stack_cap + call_stack_cap / 2;
                call_stack.resize(new_cap);
                call_stack_cap = new_cap;
            }
            if (fsp >= frame_stack_cap)
            {
                size_t new_cap = frame_stack_cap + frame_stack_cap / 2;
                frame_base_stack.resize(new_cap);
                frame_stack_cap = new_cap;
            }

            call_stack[csp++] = pc;
            frame_base_stack[fsp++] = vars_sp;

            if (vars_sp + (size_t)frame_size > vars_cap)
            {
                size_t new_cap = vars_cap + vars_cap / 2;
                if (new_cap <= vars_sp + (size_t)frame_size)
                    new_cap = vars_sp + (size_t)frame_size;
                vars.resize(new_cap);
                vars_cap = new_cap;
            }

            vars_sp += (size_t)frame_size;
            pc = addr;
            break;
        }
        case OPCODE_RET:
        {
            if (csp == 0 || fsp == 0)
            {
                bbxc::fmt::err_fmt( "Stack underflow on RET at pc=%zu\n", pc);
                return 1;
            }
            size_t old_pc = pc;
            pc = call_stack[--csp];
            vars_sp = frame_base_stack[--fsp];
            if (pc >= size)
            {
                bbxc::fmt::err_fmt( "Return address %zu out of bounds, popped at pc=%zu\n",
                        pc, old_pc);
                return 1;
            }
            break;
        }
        case OPCODE_LOADBYTE:
        {
            if (pc + 4 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for LOADBYTE at pc=%zu\n", pc);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t offset = program[pc] | (program[pc + 1] << 8) |
                              (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in LOADBYTE at pc=%zu\n", pc);
                return 1;
            }
            if (offset >= data_table_size)
            {
                bbxc::fmt::err_fmt( "Data offset out of bounds: %u\n", offset);
                exit(1);
            }
            registers[reg] = (int64_t)data_table[offset];
            break;
        }
        case OPCODE_LOADWORD:
        {
            if (pc + 4 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for LOADWORD at pc=%zu\n", pc);
                return 1;
            }

            uint8_t reg = program[pc++];
            uint32_t offset = program[pc] | (program[pc + 1] << 8) |
                              (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;

            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in LOADWORD at pc=%zu\n", pc);
                return 1;
            }

            if (offset + 1 >= data_table_size)
            {
                bbxc::fmt::err_fmt( "Data offset out of bounds: %u\n", offset);
                exit(1);
            }
            int16_t val = data_table[offset] | (data_table[offset + 1] << 8);
            registers[reg] = (int64_t)val;

            break;
        }
        case OPCODE_LOADDWORD:
        {
            if (pc + 4 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for LOADDWORD at pc=%zu\n", pc);
                return 1;
            }

            uint8_t reg = program[pc++];
            uint32_t offset = program[pc] | (program[pc + 1] << 8) |
                              (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;

            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in LOADDWORD at pc=%zu\n", pc);
                return 1;
            }

            if (offset + 3 >= data_table_size)
            {
                bbxc::fmt::err_fmt( "Data offset out of bounds: %u\n", offset);
                exit(1);
            }
            int32_t val = data_table[offset] | (data_table[offset + 1] << 8) |
                          (data_table[offset + 2] << 16) |
                          (data_table[offset + 3] << 24);
            registers[reg] = (int64_t)val;

            break;
        }
        case OPCODE_LOADQWORD:
        {
            if (pc + 4 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for LOADQWORD at pc=%zu\n", pc);
                return 1;
            }

            uint8_t reg = program[pc++];
            uint32_t offset = program[pc] | (program[pc + 1] << 8) |
                              (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;

            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in LOADQWORD at pc=%zu\n", pc);
                return 1;
            }

            if (offset + 7 >= data_table_size)
            {
                bbxc::fmt::err_fmt( "Data offset out of bounds: %u\n", offset);
                exit(1);
            }
            int64_t val = 0;
            for (int i = 0; i < 8; i++)
                val |= ((uint64_t)data_table[offset + i]) << (8 * i);
            registers[reg] = val;

            break;
        }
        case OPCODE_MOD:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for MOD at pc=%zu\n", pc);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in MOD at pc=%zu\n", pc);
                return 1;
            }
            if (registers[src] == 0)
            {
                bbxc::fmt::err_fmt( "Division by zero in MOD at pc=%zu\n", pc);
                return 1;
            }
            registers[dst] = registers[dst] % registers[src];
            break;
        }
        case OPCODE_JMPI:
        {
            if (pc + 4 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operand for JMPI at pc=%zu\n", pc);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                            (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (addr >= size)
            {
                bbxc::fmt::err_fmt( "JMPI addr out of bounds: %zu at pc=%u\n", pc, addr);
                return 1;
            }
            pc = addr;
            break;
        }
        case OPCODE_EXEC:
            REQUIRE_PRIVILEGED("EXEC")
            {
                if (pc >= size)
                {
                    bbxc::fmt::err_fmt( "Missing dest register for EXEC at pc=%zu\n", pc);
                    return 1;
                }
                uint8_t dest = program[pc++];
                if (dest >= REGISTERS)
                {
                    bbxc::fmt::err_fmt( "Invalid dest register %u for EXEC at pc=%zu\n", dest, pc);
                    return 1;
                }
                if (pc >= size)
                {
                    bbxc::fmt::err_fmt( "Missing length for EXEC at pc=%zu\n", pc);
                    return 1;
                }
                unsigned int len = program[pc++];
                if (pc + len > size)
                {
                    bbxc::fmt::err_fmt( "EXEC string past end of program at pc=%zu\n", pc);
                    return 1;
                }
                char cmd[256];
                if (len > 255)
                    len = 255;
                std::copy_n(&program[pc], len, cmd);
                cmd[len] = '\0';
                pc += len;

                if (debug)
                    std::println("[DEBUG] EXEC: {} -> r{:02}", cmd, dest);

                int ret = std::system(cmd);
                registers[dest] = (int64_t)ret;
                break;
            }

        case OPCODE_DROPPRIV:
            REQUIRE_PRIVILEGED("DROPPPRIV")
            {
                cur_mode = MODE_PROTECTED;
                break;
            }

        case OPCODE_GETMODE:
        {
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in GETMODE at pc=%zu\n", pc);
                goto fault_exit;
            }
            registers[reg] = (cur_mode == MODE_PROTECTED) ? 0 : 1;
            break;
        }

        case OPCODE_REGSYSCALL:
            REQUIRE_PRIVILEGED("REGSYSCALL")
            {
                unsigned int id = program[pc++];
                uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                                (program[pc + 2] << 16) | (program[pc + 3] << 24);

                pc += 4;
                if (id >= MAX_SYSCALLS)
                {
                    RAISE_FAULT(FAULT_BAD_SYSCALL, "SYSCALL %u not registered at pc=%zu", id, pc);
                    break;
                }
                syscall_table[id] = addr;
                syscall_registered[id] = true;
                break;
            }

        case OPCODE_SYSCALL:
        {
            if (cur_mode != MODE_PROTECTED)
            {
                bbxc::fmt::err_fmt( "FAULT: SYSCALL only allowed in protected mode at pc=%zu\n", pc);
                goto fault_exit;
            }

            unsigned int id = program[pc++];
            if (id >= MAX_SYSCALLS)
            {
                bbxc::fmt::err_fmt( "FAULT: Invalid syscall ID %u at pc=%zu\n", id, pc);
                goto fault_exit;
            }

            if (!syscall_registered[id])
            {
                bbxc::fmt::err_fmt( "FAULT: SYSCALL %u not registered at pc=%zu\n", id, pc);
                goto fault_exit;
            }

            syscall_return_pc = pc;
            cur_mode = MODE_PRIVILEGED;
            pc = syscall_table[id];
            break;
        }

        case OPCODE_SYSRET:
            REQUIRE_PRIVILEGED("SYSRET")
            {
                cur_mode = MODE_PROTECTED;
                pc = syscall_return_pc;
                break;
            }

        case OPCODE_SETPERM:
            REQUIRE_PRIVILEGED("SETPERM")
            {
                uint32_t start = program[pc] | (program[pc + 1] << 8) |
                                 (program[pc + 2] << 16) | (program[pc + 3] << 24);
                pc += 4;

                uint32_t count = program[pc] | (program[pc + 1] << 8) |
                                 (program[pc + 2] << 16) | (program[pc + 3] << 24);
                pc += 4;

                uint8_t priv_read = program[pc++];
                uint8_t priv_write = program[pc++];
                uint8_t prot_read = program[pc++];
                uint8_t prot_write = program[pc++];

                for (uint32_t i = 0; i < count; i++)
                {
                    size_t idx = start + i;
                    if (idx >= stack_cap)
                        break;
                    permissions[idx].priv_read = priv_read;
                    permissions[idx].priv_write = priv_write;
                    permissions[idx].prot_read = prot_read;
                    permissions[idx].prot_write = prot_write;
                }
                break;
            }

        case OPCODE_REGFAULT:
            REQUIRE_PRIVILEGED("REGFAULT")
            {
                uint8_t fault_id = program[pc++];
                uint32_t addr = program[pc] | (program[pc + 1] << 8) |
                                (program[pc + 2] << 16) | (program[pc + 3] << 24);
                pc += 4;

                if (fault_id >= FAULT_COUNT)
                {
                    bbxc::fmt::err_fmt( "Invalid fault ID %u in REGFAULT at pc=%zu\n", fault_id, pc);
                    goto fault_exit;
                }

                fault_table[fault_id] = addr;
                fault_registered[fault_id] = true;
                break;
            }

        case OPCODE_FAULTRET:
            REQUIRE_PRIVILEGED("FAULTRET")
            {
                if (current_fault == FAULT_COUNT)
                {
                    bbxc::fmt::err_fmt( "FAULTRET executed but no fault is active at pc=%zu\n", pc);
                    goto fault_exit;
                }

                current_fault = FAULT_COUNT;
                cur_mode = MODE_PROTECTED;
                pc = fault_return_pc;
                break;
            }

        case OPCODE_GETFAULT:
        {
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in GETFAULT at pc=%zu\n", pc);
                goto fault_exit;
            }
            registers[reg] = (int64_t)current_fault;
            break;
        }

        case OPCODE_DUMPREGS:
        {
            print_regs(registers, REGISTERS);
            break;
        }

        case OPCODE_PRINTCHAR:
        {
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in PRINTCHAR at pc=%zu\n", pc);
                goto fault_exit;
            }
            putchar((char)registers[reg]);
            fflush(stdout);
            break;
        }
        case OPCODE_EPRINTCHAR:
        {
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in EPRINTCHAR at pc=%zu\n", pc);
                goto fault_exit;
            }
            fputc((char)registers[reg], stderr);
            fflush(stderr);
            break;
        }
        case OPCODE_SHLI:
        {
            uint8_t reg = program[pc++];
            uint64_t shift = read_u64(program.data(), &pc);
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in SHLI at pc=%zu\n", pc);
                goto fault_exit;
            }
            if (shift >= 64)
                registers[reg] = 0;
            else
                registers[reg] = registers[reg] << shift;
            break;
        }
        case OPCODE_SHRI:
        {
            uint8_t reg = program[pc++];
            uint64_t shift = read_u64(program.data(), &pc);
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in SHRI at pc=%zu\n", pc);
                goto fault_exit;
            }
            if (shift >= 64)
            {
                if (registers[reg] < 0)
                    registers[reg] = -1;
                else
                    registers[reg] = 0;
            }
            else
            {
                registers[reg] = registers[reg] >> shift;
            }
            break;
        }
        case OPCODE_SHR:
        {
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in SHR at pc=%zu\n", pc);
                goto fault_exit;
            }
            if (registers[src] >= 64)
            {
                if (registers[dst] < 0)
                    registers[dst] = -1;
                else
                    registers[dst] = 0;
            }
            else
            {
                registers[dst] = registers[dst] >> registers[src];
            }
            break;
        }
        case OPCODE_SHL:
        {
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in SHL at pc=%zu\n", pc);
                goto fault_exit;
            }
            if (registers[src] >= 64)
            {
                if (registers[dst] < 0)
                    registers[dst] = -1;
                else
                    registers[dst] = 0;
            }
            else
            {
                registers[dst] = registers[dst] << registers[src];
            }
            break;
        }
        case OPCODE_GETARGC:
        {
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in GETARGC at pc=%zu\n", pc);
                goto fault_exit;
            }
            registers[reg] = (int64_t)argc;
            break;
        }
        case OPCODE_GETARG:
        {
            if (pc + 4 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for GETARG at pc=%zu\n", pc);
                goto fault_exit;
            }
            uint8_t reg = program[pc++];
            uint32_t idx = read_u32(program.data(), &pc);
            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in GETARG at pc=%zu\n", pc);
                goto fault_exit;
            }
            if ((size_t)idx >= (size_t)argc)
            {
                bbxc::fmt::err_fmt( "GETARG index out of bounds: %u at pc=%zu\n", idx, pc);
                goto fault_exit;
            }
            const char *arg = argv[idx];
            if (!arg)
            {
                bbxc::fmt::err_fmt( "GETARG null argument at index %u at pc=%zu\n", idx, pc);
                goto fault_exit;
            }
            size_t len = std::string_view(arg).size();
            if (str_heap_size > 0x7FFFFFFF || str_heap_size + len + 1 > 0x7FFFFFFF)
            {
                bbxc::fmt::err_fmt( "String heap too large at pc=%zu\n", pc);
                goto fault_exit;
            }
            if (ensure_capacity(str_heap, str_heap_size + len + 1))
            {
                bbxc::fmt::err_errno("realloc");
                goto fault_exit;
            }
            uint32_t start_addr = 0x80000000u | (uint32_t)str_heap_size;
            std::copy_n(arg, len, &str_heap[str_heap_size]);
            str_heap_size += len;
            str_heap[str_heap_size++] = 0;
            registers[reg] = (int64_t)start_addr;
            break;
        }
        case OPCODE_GETENV:
        {
            if (pc + 1 >= size)
            {
                bbxc::fmt::err_fmt( "Missing operands for GETENV at pc=%zu\n", pc);
                goto fault_exit;
            }
            uint8_t reg = program[pc++];
            uint8_t envlen = program[pc++];
            if (pc + envlen > size)
            {
                bbxc::fmt::err_fmt( "Environment variable name past end of program at pc=%zu\n", pc);
                goto fault_exit;
            }
            char envname[256];
            if (envlen > sizeof(envname) - 1)
                envlen = sizeof(envname) - 1;
            std::copy_n(&program[pc], envlen, envname);
            envname[envlen] = '\0';
            pc += envlen;

            if (reg >= REGISTERS)
            {
                bbxc::fmt::err_fmt( "Invalid register in GETENV at pc=%zu\n", pc);
                goto fault_exit;
            }

            const char *env_value = std::getenv(envname);
            if (!env_value)
            {
                RAISE_FAULT(FAULT_ENV_VAR_NOT_FOUND, "Environment variable %s not found in GETENV at pc=%zu", envname, pc);
                break;
            }

            size_t value_len = std::string_view(env_value).size();
            if (str_heap_size > 0x7FFFFFFF || str_heap_size + value_len + 1 > 0x7FFFFFFF)
            {
                bbxc::fmt::err_fmt( "String heap too large at pc=%zu\n", pc);
                goto fault_exit;
            }
            if (ensure_capacity(str_heap, str_heap_size + value_len + 1))
            {
                bbxc::fmt::err_errno("realloc");
                goto fault_exit;
            }
            uint32_t start_addr = 0x80000000u | (uint32_t)str_heap_size;
            std::copy_n(env_value, value_len, &str_heap[str_heap_size]);
            str_heap_size += value_len;
            str_heap[str_heap_size++] = 0;
            registers[reg] = (int64_t)start_addr;
            break;
        }
        default:
        {
            bbxc::fmt::err_fmt( "Unknown opcode 0x%02X at position %zu\n", opcode,
                    pc - 1);
            return 1;
        }
        }
    }

    (void)AF;
    (void)PF;
    (void)data_count;
    return 0;

fault_exit:
    return 1;
}
