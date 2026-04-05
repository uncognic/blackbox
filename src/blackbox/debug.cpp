#include "debug.h"
#include "../define.h"
#include <cstddef>
#include <cstdint>
#include <print>

const char* opcode_name(uint8_t op) {
    switch (opcode_from_byte(op)) {
        case Opcode::WRITE:
            return "WRITE";
        case Opcode::NEWLINE:
            return "NEWLINE";
        case Opcode::PRINT:
            return "PRINT";
        case Opcode::PUSHI:
            return "PUSH_IMM";
        case Opcode::POP:
            return "POP";
        case Opcode::ADD:
            return "ADD";
        case Opcode::SUB:
            return "SUB";
        case Opcode::MUL:
            return "MUL";
        case Opcode::DIV:
            return "DIV";
        case Opcode::PRINTREG:
            return "PRINTREG";
        case Opcode::MOVI:
            return "MOVI";
        case Opcode::MOV_REG:
            return "MOV_REG";
        case Opcode::JMP:
            return "JMP";
        case Opcode::JE:
            return "JE";
        case Opcode::JNE:
            return "JNE";
        case Opcode::INC:
            return "INC";
        case Opcode::DEC:
            return "DEC";
        case Opcode::PUSH_REG:
            return "PUSH_REG";
        case Opcode::CMP:
            return "CMP";
        case Opcode::ALLOC:
            return "ALLOC";
        case Opcode::LOAD:
            return "LOAD";
        case Opcode::STORE:
            return "STORE";
        case Opcode::LOAD_REG:
            return "LOAD_REG";
        case Opcode::STORE_REG:
            return "STORE_REG";
        case Opcode::LOADVAR:
            return "LOADVAR";
        case Opcode::STOREVAR:
            return "STOREVAR";
        case Opcode::LOADVAR_REG:
            return "LOADVAR_REG";
        case Opcode::STOREVAR_REG:
            return "STOREVAR_REG";
        case Opcode::GROW:
            return "GROW";
        case Opcode::PRINT_STACKSIZE:
            return "PRINT_STACKSIZE";
        case Opcode::RESIZE:
            return "RESIZE";
        case Opcode::FREE:
            return "FREE";
        case Opcode::FOPEN:
            return "FOPEN";
        case Opcode::FCLOSE:
            return "FCLOSE";
        case Opcode::FREAD:
            return "FREAD";
        case Opcode::FWRITE_REG:
            return "FWRITE_REG";
        case Opcode::FWRITE_IMM:
            return "FWRITE_IMM";
        case Opcode::FSEEK_REG:
            return "FSEEK_REG";
        case Opcode::FSEEK_IMM:
            return "FSEEK_IMM";
        case Opcode::LOADSTR:
            return "LOADSTR";
        case Opcode::PRINTSTR:
            return "PRINTSTR";
        case Opcode::XOR:
            return "XOR";
        case Opcode::AND:
            return "AND";
        case Opcode::OR:
            return "OR";
        case Opcode::NOT:
            return "NOT";
        case Opcode::READSTR:
            return "READSTR";
        case Opcode::READ:
            return "READ";
        case Opcode::SLEEP:
            return "SLEEP";
        case Opcode::SLEEP_REG:
            return "SLEEP_REG";
        case Opcode::CLRSCR:
            return "CLRSCR";
        case Opcode::RAND:
            return "RAND";
        case Opcode::GETKEY:
            return "GETKEY";
        case Opcode::CONTINUE:
            return "CONTINUE";
        case Opcode::READCHAR:
            return "READCHAR";
        case Opcode::JL:
            return "JL";
        case Opcode::JGE:
            return "JGE";
        case Opcode::JB:
            return "JB";
        case Opcode::JAE:
            return "JAE";
        case Opcode::CALL:
            return "CALL";
        case Opcode::RET:
            return "RET";
        case Opcode::LOADBYTE:
            return "LOADBYTE";
        case Opcode::LOADWORD:
            return "LOADWORD";
        case Opcode::LOADDWORD:
            return "LOADDWORD";
        case Opcode::LOADQWORD:
            return "LOADQWORD";
        case Opcode::MOD:
            return "MOD";
        case Opcode::JMPI:
            return "JMPI";
        case Opcode::EXEC:
            return "EXEC";
        case Opcode::SYSCALL:
            return "SYSCALL";
        case Opcode::SYSRET:
            return "SYSRET";
        case Opcode::DROPPRIV:
            return "DROPPRIV";
        case Opcode::REGSYSCALL:
            return "REGSYSCALL";
        case Opcode::SETPERM:
            return "SETPERM";
        case Opcode::GETMODE:
            return "GETMODE";
        case Opcode::REGFAULT:
            return "REGFAULT";
        case Opcode::FAULTRET:
            return "FAULTRET";
        case Opcode::GETFAULT:
            return "GETFAULT";
        case Opcode::DUMPREGS:
            return "DUMPREGS";
        case Opcode::PRINTCHAR:
            return "PRINTCHAR";
        case Opcode::EPRINTREG:
            return "EPRINTREG";
        case Opcode::EPRINTSTR:
            return "EPRINTSTR";
        case Opcode::EPRINTCHAR:
            return "EPRINTCHAR";
        case Opcode::SHLI:
            return "SHLI";
        case Opcode::SHRI:
            return "SHRI";
        case Opcode::SHR:
            return "SHR";
        case Opcode::SHL:
            return "SHL";
        case Opcode::GETARG:
            return "GETARG";
        case Opcode::GETARGC:
            return "GETARGC";
        case Opcode::GETENV:
            return "GETENV";
        case Opcode::BREAK:
            return "BREAK";
        case Opcode::HALT:
            return "HALT";
        default:
            return "UNKNOWN";
    }
}

void print_regs(const int64_t* regs, int count) {
    if (count <= 0) count = 16;
    if (count > REGISTERS) count = REGISTERS;
    std::print("Registers (first {}):\n", count);
    for (int i = 0; i < count; i++) std::print(" r{:02}={}\n", i, regs[i]);
}

void print_stack(const int64_t* stack, size_t sp) {
    size_t show = sp < 8 ? sp : 8;
    std::print("Stack size={}, top {} entries:\n", sp, show);
    for (size_t i = 0; i < show; i++) {
        size_t idx = (sp == 0) ? 0 : sp - 1 - i;
        std::print(" [{}]={}\n", idx, stack[idx]);
    }
}
