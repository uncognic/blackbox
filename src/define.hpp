#pragma once

#include <cstddef>
#include <cstdint>

// binary format constants
constexpr uint32_t MAGIC = 0x626378U;
constexpr size_t MAGIC_SIZE = 3;

// header: magic(3) | global_count(4) | entry_count(4)
constexpr size_t HEADER_FIXED_SIZE = 11;

// VM limits
constexpr size_t REGISTERS = 99;
constexpr size_t FILE_DESCRIPTORS = 99;
constexpr size_t MAX_SYSCALLS = 256;

// data section entry types
enum class DataEntryType : uint8_t {
    String = 0,
};

// privilege mode
enum class Mode : uint8_t {
    Protected,
    Privileged,
};

// slot permissions
struct SlotPermission {
    uint8_t priv_read : 1;
    uint8_t priv_write : 1;
    uint8_t prot_read : 1;
    uint8_t prot_write : 1;
};

enum class OperandType : uint8_t {
    Reg = 0x00,    // 1 byte:  register index
    Imm = 0x01,    // 4 bytes: i32
    Imm64 = 0x02,  // 8 bytes: i64
    Bss = 0x03,    // 4 bytes: bss slot (deref, [name])
    BssRef = 0x04, // 4 bytes: bss slot (address-of, bare name)
    Var = 0x05,    // 4 bytes: frame slot
    Data = 0x06,   // 4 bytes: data index
};

enum class Opcode : uint8_t {
    MOV = 0x01,
    ADD = 0x02,
    SUB = 0x03,
    MUL = 0x04,
    DIV = 0x05,
    MOD = 0x06,
    INC = 0x07,
    DEC = 0x08,
    AND = 0x09,
    OR = 0x0A,
    XOR = 0x0B,
    NOT = 0x0C,
    SHL = 0x0D,
    SHR = 0x0E,
    CMP = 0x11,
    PUSH = 0x12,
    POP = 0x14,
    JMP = 0x15,
    JMPI = 0x16,
    JE = 0x17,
    JNE = 0x18,
    JL = 0x19,
    JGE = 0x1A,
    JB = 0x1B,
    JAE = 0x1C,
    CALL = 0x1D,
    RET = 0x1E,
    HALT = 0x1F,
    LOAD = 0x20,
    STORE = 0x22,
    LOADREF = 0x24,
    STOREREF = 0x25,
    LOADSTR = 0x26,
    ALLOC = 0x27,
    GROW = 0x28,
    RESIZE = 0x29,
    FREE = 0x2A,
    WRITE = 0x30,
    PRINT = 0x31,
    NEWLINE = 0x32,
    PRINTREG = 0x33,
    EPRINTREG = 0x34,
    PRINTSTR = 0x35,
    EPRINTSTR = 0x36,
    PRINTCHAR = 0x37,
    EPRINTCHAR = 0x38,
    READ = 0x39,
    READSTR = 0x3A,
    READCHAR = 0x3B,
    FOPEN = 0x40,
    FCLOSE = 0x41,
    FREAD = 0x42,
    FWRITE = 0x43,
    FSEEK = 0x45,
    EXEC = 0x50,
    SLEEP = 0x51,
    RAND = 0x53,
    GETKEY = 0x54,
    CLRSCR = 0x55,
    GETARG = 0x56,
    GETARGC = 0x57,
    GETENV = 0x58,
    SYSCALL = 0x60,
    SYSRET = 0x61,
    DROPPRIV = 0x62,
    REGSYSCALL = 0x63,
    SETPERM = 0x64,
    GETMODE = 0x65,
    REGFAULT = 0x66,
    FAULTRET = 0x67,
    GETFAULT = 0x68,
    BREAK = 0xFD,
    CONTINUE = 0xFE,
    DUMPREGS = 0xF0,
    PRINT_STACKSIZE = 0xF1,
};

inline constexpr uint8_t opcode_to_byte(Opcode op) {
    return static_cast<uint8_t>(op);
}

inline constexpr Opcode opcode_from_byte(uint8_t b) {
    return static_cast<Opcode>(b);
}