#include "debug.h"
#include "../define.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
const char *opcode_name(uint8_t op) {
  switch (op) {
  case OPCODE_WRITE:
    return "WRITE";
  case OPCODE_NEWLINE:
    return "NEWLINE";
  case OPCODE_PRINT:
    return "PRINT";
  case OPCODE_PUSH_IMM:
    return "PUSH_IMM";
  case OPCODE_POP:
    return "POP";
  case OPCODE_ADD:
    return "ADD";
  case OPCODE_SUB:
    return "SUB";
  case OPCODE_MUL:
    return "MUL";
  case OPCODE_DIV:
    return "DIV";
  case OPCODE_PRINTREG:
    return "PRINTREG";
  case OPCODE_MOV_IMM:
    return "MOV_IMM";
  case OPCODE_MOV_REG:
    return "MOV_REG";
  case OPCODE_JMP:
    return "JMP";
  case OPCODE_JE:
    return "JE";
  case OPCODE_JNE:
    return "JNE";
  case OPCODE_INC:
    return "INC";
  case OPCODE_DEC:
    return "DEC";
  case OPCODE_PUSH_REG:
    return "PUSH_REG";
  case OPCODE_CMP:
    return "CMP";
  case OPCODE_ALLOC:
    return "ALLOC";
  case OPCODE_LOAD:
    return "LOAD";
  case OPCODE_STORE:
    return "STORE";
  case OPCODE_LOAD_REG:
    return "LOAD_REG";
  case OPCODE_STORE_REG:
    return "STORE_REG";
  case OPCODE_LOADVAR:
    return "LOADVAR";
  case OPCODE_STOREVAR:
    return "STOREVAR";
  case OPCODE_LOADVAR_REG:
    return "LOADVAR_REG";
  case OPCODE_STOREVAR_REG:
    return "STOREVAR_REG";
  case OPCODE_GROW:
    return "GROW";
  case OPCODE_PRINT_STACKSIZE:
    return "PRINT_STACKSIZE";
  case OPCODE_RESIZE:
    return "RESIZE";
  case OPCODE_FREE:
    return "FREE";
  case OPCODE_FOPEN:
    return "FOPEN";
  case OPCODE_FCLOSE:
    return "FCLOSE";
  case OPCODE_FREAD:
    return "FREAD";
  case OPCODE_FWRITE_REG:
    return "FWRITE_REG";
  case OPCODE_FWRITE_IMM:
    return "FWRITE_IMM";
  case OPCODE_FSEEK_REG:
    return "FSEEK_REG";
  case OPCODE_FSEEK_IMM:
    return "FSEEK_IMM";
  case OPCODE_LOADSTR:
    return "LOADSTR";
  case OPCODE_PRINTSTR:
    return "PRINTSTR";
  case OPCODE_XOR:
    return "XOR";
  case OPCODE_AND:
    return "AND";
  case OPCODE_OR:
    return "OR";
  case OPCODE_NOT:
    return "NOT";
  case OPCODE_READSTR:
    return "READSTR";
  case OPCODE_READ:
    return "READ";
  case OPCODE_SLEEP:
    return "SLEEP";
  case OPCODE_CLRSCR:
    return "CLRSCR";
  case OPCODE_RAND:
    return "RAND";
  case OPCODE_GETKEY:
    return "GETKEY";
  case OPCODE_CONTINUE:
    return "CONTINUE";
  case OPCODE_READCHAR:
    return "READCHAR";
  case OPCODE_JL:
    return "JL";
  case OPCODE_JGE:
    return "JGE";
  case OPCODE_JB:
    return "JB";
  case OPCODE_JAE:
    return "JAE";
  case OPCODE_CALL:
    return "CALL";
  case OPCODE_RET:
    return "RET";
  case OPCODE_LOADBYTE:
    return "LOADBYTE";
  case OPCODE_LOADWORD:
    return "LOADWORD";
  case OPCODE_LOADDWORD:
    return "LOADDWORD";
  case OPCODE_LOADQWORD:
    return "LOADQWORD";
  case OPCODE_MOD:
    return "MOD";
  case OPCODE_BREAK:
    return "BREAK";
  case OPCODE_HALT:
    return "HALT";
  default:
    return "UNKNOWN";
  }
}

void print_regs(const int64_t *regs, int count) {
  if (count <= 0)
    count = 16;
  if (count > REGISTERS)
    count = REGISTERS;
  printf("Registers (first %d):\n", count);
  for (int i = 0; i < count; i++)
    printf(" r%02d=%lld", i, (long long)regs[i]);
  printf("\n");
}

void print_stack(const int64_t *stack, size_t sp) {
  size_t show = sp < 8 ? sp : 8;
  printf("Stack size=%zu, top %zu entries:\n", sp, show);
  for (size_t i = 0; i < show; i++) {
    size_t idx = (sp == 0) ? 0 : sp - 1 - i;
    printf(" [%zu]=%lld", idx, (long long)stack[idx]);
  }
  printf("\n");
}
