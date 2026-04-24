#include "debug.hpp"
#include "../define.hpp"
#include <cstdint>
#include <print>

const char* opcode_name(uint8_t op) {
    switch (opcode_from_byte(op)) {
        case Opcode::MOV:
            return "MOV";
        case Opcode::ADD:
            return "ADD";
        case Opcode::SUB:
            return "SUB";
        case Opcode::MUL:
            return "MUL";
        case Opcode::DIV:
            return "DIV";
        case Opcode::MOD:
            return "MOD";
        case Opcode::INC:
            return "INC";
        case Opcode::DEC:
            return "DEC";
        case Opcode::AND:
            return "AND";
        case Opcode::OR:
            return "OR";
        case Opcode::XOR:
            return "XOR";
        case Opcode::NOT:
            return "NOT";
        case Opcode::SHL:
            return "SHL";
        case Opcode::SHR:
            return "SHR";
        case Opcode::CMP:
            return "CMP";
        case Opcode::POP:
            return "POP";
        case Opcode::JMP:
            return "JMP";
        case Opcode::JMPI:
            return "JMPI";
        case Opcode::JE:
            return "JE";
        case Opcode::JNE:
            return "JNE";
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
        case Opcode::HALT:
            return "HALT";
        case Opcode::LOAD:
            return "LOAD";
        case Opcode::STORE:
            return "STORE";
        case Opcode::LOADREF:
            return "LOADREF";
        case Opcode::STOREREF:
            return "STOREREF";
        case Opcode::LOADSTR:
            return "LOADSTR";
        case Opcode::ALLOC:
            return "ALLOC";
        case Opcode::GROW:
            return "GROW";
        case Opcode::RESIZE:
            return "RESIZE";
        case Opcode::FREE:
            return "FREE";
        case Opcode::WRITE:
            return "WRITE";
        case Opcode::PRINT:
            return "PRINT";
        case Opcode::NEWLINE:
            return "NEWLINE";
        case Opcode::PRINTREG:
            return "PRINTREG";
        case Opcode::EPRINTREG:
            return "EPRINTREG";
        case Opcode::PRINTSTR:
            return "PRINTSTR";
        case Opcode::EPRINTSTR:
            return "EPRINTSTR";
        case Opcode::PRINTCHAR:
            return "PRINTCHAR";
        case Opcode::EPRINTCHAR:
            return "EPRINTCHAR";
        case Opcode::READ:
            return "READ";
        case Opcode::READSTR:
            return "READSTR";
        case Opcode::READCHAR:
            return "READCHAR";
        case Opcode::FOPEN:
            return "FOPEN";
        case Opcode::FCLOSE:
            return "FCLOSE";
        case Opcode::FREAD:
            return "FREAD";
        case Opcode::EXEC:
            return "EXEC";
        case Opcode::SLEEP:
            return "SLEEP";
        case Opcode::RAND:
            return "RAND";
        case Opcode::GETKEY:
            return "GETKEY";
        case Opcode::CLRSCR:
            return "CLRSCR";
        case Opcode::GETARG:
            return "GETARG";
        case Opcode::GETARGC:
            return "GETARGC";
        case Opcode::GETENV:
            return "GETENV";
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
        case Opcode::BREAK:
            return "BREAK";
        case Opcode::CONTINUE:
            return "CONTINUE";
        case Opcode::DUMPREGS:
            return "DUMPREGS";
        case Opcode::PRINT_STACKSIZE:
            return "PRINT_STACKSIZE";
        case Opcode::PUSH:
            return "PUSH";
        case Opcode::FWRITE:
            return "FWRITE";
        case Opcode::FSEEK:
            return "FSEEK";
        default:
            return "UNKNOWN";
    }
}
void print_regs(const int64_t* regs, int count) {
    if (count <= 0) {
        count = 16;
    }
    if (count > REGISTERS) {
        count = REGISTERS;
    }
    std::print("Registers (first {}):\n", count);
    for (int i = 0; i < count; i++) {
        std::print(" r{:02}={}\n", i, regs[i]);
    }
}

void print_stack(const int64_t* stack, size_t sp) {
    size_t show = sp < 8 ? sp : 8;
    std::print("Stack size={}, top {} entries:\n", sp, show);
    for (size_t i = 0; i < show; i++) {
        size_t idx = (sp == 0) ? 0 : sp - 1 - i;
        std::print(" [{}]={}\n", idx, stack[idx]);
    }
}
