#pragma once

#include <cstdint>
#include <cstddef>

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

enum class Opcode : uint8_t {
    WRITE = 0x01,
    NEWLINE = 0x02,
    PRINT = 0x03,
    PUSHI = 0x04,
    POP = 0x05,
    ADD = 0x06,
    SUB = 0x07,
    MUL = 0x08,
    DIV = 0x09,
    PRINTREG = 0x0A,
    MOVI = 0x0B,
    MOV_REG = 0x0C,
    JMP = 0x0D,
    JE = 0x0E,
    JNE = 0x0F,
    INC = 0x10,
    DEC = 0x11,
    PUSH_REG = 0x12,
    CMP = 0x13,
    ALLOC = 0x14,
    LOAD = 0x15,
    STORE = 0x16,
    LOAD_REG = 0x41,
    STORE_REG = 0x42,
    LOADVAR = 0x43,
    STOREVAR = 0x44,
    LOADVAR_REG = 0x45,
    STOREVAR_REG = 0x46,
    GROW = 0x17,
    PRINT_STACKSIZE = 0x18,
    RESIZE = 0x19,
    FREE = 0x20,
    FOPEN = 0x21,
    FCLOSE = 0x22,
    FREAD = 0x23,
    FWRITE_REG = 0x24,
    FSEEK_REG = 0x25,
    FSEEK_IMM = 0x26,
    FWRITE_IMM = 0x27,
    LOADSTR = 0x28,
    PRINTSTR = 0x29,
    XOR = 0x2A,
    AND = 0x2B,
    OR = 0x2C,
    NOT = 0x2D,
    READSTR = 0x2E,
    READ = 0x33,
    SLEEP = 0x2F,
    SLEEP_REG = 0x60,
    CLRSCR = 0x30,
    RAND = 0x31,
    GETKEY = 0x32,
    CONTINUE = 0x34,
    READCHAR = 0x35,
    JL = 0x36,
    JGE = 0x37,
    JB = 0x38,
    JAE = 0x39,
    CALL = 0x3A,
    RET = 0x3B,
    MOD = 0x40,
    JMPI = 0x47,
    EXEC = 0x48,
    SYSCALL = 0x50,
    SYSRET = 0x51,
    DROPPRIV = 0x52,
    REGSYSCALL = 0x53,
    SETPERM = 0x54,
    GETMODE = 0x55,
    REGFAULT = 0x56,
    FAULTRET = 0x57,
    GETFAULT = 0x58,
    DUMPREGS = 0x59,
    PRINTCHAR = 0x5A,
    EPRINTREG = 0x5B,
    EPRINTSTR = 0x5C,
    EPRINTCHAR = 0x5D,
    SHLI = 0x5E,
    SHRI = 0x5F,
    SHR = 0x61,
    SHL = 0x62,
    GETARG = 0x63,
    GETARGC = 0x64,
    GETENV = 0x65,
    LOADREF = 0x66,
    STOREREF = 0x67,
    LOADGLOBAL = 0x68,
    STOREGLOBAL = 0x69,
    HALT = 0xFF,
    BREAK = 0xFE,
};

inline constexpr uint8_t opcode_to_byte(Opcode op) {
    return static_cast<uint8_t>(op);
}

inline constexpr Opcode opcode_from_byte(uint8_t b) {
    return static_cast<Opcode>(b);
}