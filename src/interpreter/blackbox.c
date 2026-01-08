#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "../define.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: bbx program.bcx");
        return 1;
    }
    int64_t registers[REGISTERS] = {0};
    size_t sp = 0;
    size_t stack_cap = STACK_SIZE;
    int64_t *stack = NULL;
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *program = malloc(size);
    if (!program) {
        perror("malloc");
        fclose(f);
        return 1;
    }
    size_t n = fread(program, 1, size, f);
    if (n != size) {
        perror("fread");
        free(program);
        fclose(f);
        return 1;
    }
    if (size < 3) {
        fprintf(stderr, "Error: program too small (missing magic)\n");
        free(program);
        fclose(f);
        return 1;
    }
    uint8_t m0 = (MAGIC >> 16) & 0xFF;
    uint8_t m1 = (MAGIC >> 8) & 0xFF;
    uint8_t m2 = (MAGIC) & 0xFF;
    if (program[0] != m0 || program[1] != m1 || program[2] != m2) {
        fprintf(stderr, "Error: invalid magic (expected '%c%c%c')\n", m0, m1, m2);
        free(program);
        fclose(f);
        return 1;
    }

    fclose(f);

    stack = malloc(stack_cap * sizeof *stack);
    if (!stack) {
        perror("malloc");
        free(program);
        return 1;
    }

    size_t pc = 3;

    while (pc < size) {
        uint8_t opcode = program[pc++];
        switch (opcode) {
            case OPCODE_WRITE: {
                uint8_t fd = program[pc++];
                uint8_t len = program[pc++];
                if (fd != 1 && fd != 2) {
                    fprintf(stderr, "Error: invalid fd %lu at pc=%u\n", pc, fd);
                    free(program);
                    free(stack);
                    return 1;
                }
                if (pc + len > size) {
                    fprintf(stderr, "Error: string past end of program at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                if (write(fd, &program[pc], len) != len) {
                    perror("write");
                    free(program);
                    free(stack);
                    return 1;
                }
                pc += len;
                break;
            }
            case OPCODE_INC: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for INC at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t reg = program[pc++];
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register in INC at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                registers[reg] += 1;
                break;
            }
            case OPCODE_DEC: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for DEC at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t reg = program[pc++];
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register in DEC at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                registers[reg] -= 1;
                break;
            }
            case OPCODE_PUSH_IMM: {
                if (pc + 3 >= size) {
                    fprintf(stderr, "Missing operand for PUSH at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                if (sp >= stack_cap) {
                    size_t new_cap = stack_cap + stack_cap/2;
                    if (new_cap <= sp) new_cap = sp + 1;
                    int64_t *tmp = realloc(stack, new_cap * sizeof *stack);
                    if (!tmp) {
                        perror("realloc");
                        free(program);
                        free(stack);
                        return 1;
                    }
                    stack = tmp;
                    stack_cap = new_cap;
                }
                int32_t value = program[pc] | (program[pc+1]<<8) | (program[pc+2]<<16) | (program[pc+3]<<24);
                pc += 4;

                stack[sp++] = value;
                break;
            }
            case OPCODE_PUSH_REG: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operands for PUSH_REG at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t src = program[pc++];
                if (src >= REGISTERS) {
                    fprintf(stderr, "Invalid register in PUSH_REG at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                if (sp >= stack_cap) {
                    size_t new_cap = stack_cap + stack_cap/2;
                    if (new_cap <= sp) new_cap = sp + 1;
                    int64_t *tmp = realloc(stack, new_cap * sizeof *stack);
                    if (!tmp) {
                        perror("realloc");
                        free(program);
                        free(stack);
                        return 1;
                    }
                    stack = tmp;
                    stack_cap = new_cap;
                }
                stack[sp++] = registers[src];
                break;
            }
            case OPCODE_CMP: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for CMP at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in CMP at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                int32_t result = registers[dst] - registers[src];
                if (result < 0) { 
                    registers[REGISTERS-1] = 0;
                } else if (result > 0) {
                    registers[REGISTERS-1] = 1;
                } else {
                    registers[REGISTERS-1] = 0;
                }
                break;
            }
            case OPCODE_POP: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for POP at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t reg = program[pc++];
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register %u at pc=%zu\n", reg, pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                if (sp == 0) {
                    fprintf(stderr, "Stack underflow at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                registers[reg] = stack[--sp];
                break;
            }
            case OPCODE_JZ: {
                if (pc + 4 >= size) {
                    fprintf(stderr, "Missing operands for JZ at pc=%zu at pc=%zu\n", pc, pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t reg = program[pc++];
                uint32_t addr = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc += 4;
                if (registers[reg] == 0) {
                    if (addr >= size) {
                        fprintf(stderr, "JZ address out of bounds: %u at pc=%zu\n", addr, pc);
                        free(program);
                        free(stack);
                        return 1;
                    }
                    pc = addr;
                }
                break;
            }
            case OPCODE_JNZ: {
                if (pc + 4 >= size) {
                    fprintf(stderr, "Missing operands for JNZ at pc=%zu at pc=%zu\n", pc, pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t reg = program[pc++];
                uint32_t addr = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc += 4;
                if (registers[reg] != 0) {
                    if (addr >= size) {
                        fprintf(stderr, "JNZ address out of bounds: %u at pc=%zu\n", addr, pc);
                        free(program);
                        free(stack);
                        return 1;
                    }
                    pc = addr;
                }
                break;
            }
            case OPCODE_ADD: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for ADD at pc=%zu at pc=%zu\n", pc, pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t dst = program[pc++];
                uint8_t src = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in ADD at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                registers[dst] += registers[src];
                break;
            }
            case OPCODE_SUB: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for ADD at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t dst = program[pc++];
                uint8_t src = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in ADD at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                registers[dst] -= registers[src];
                break;
            }
            case OPCODE_MUL: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for MUL at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t dst = program[pc++];
                uint8_t src = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in MUL at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                registers[dst] *= registers[src];
                break;
            }
            case OPCODE_DIV: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for DIV at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t dst = program[pc++];
                uint8_t src = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in DIV at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                if (registers[src] == 0) {
                    fprintf(stderr, "Invalid: division by zero at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                registers[dst] /= registers[src];
                break;
            }

            case OPCODE_PRINTREG: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for PRINT_REG at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }

                uint8_t reg = program[pc++];
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register");
                    free(program);
                    free(stack);
                    return 1;
                }
                printf("%lld", (long long)registers[reg]);
                fflush(stdout);
                break;
            }
            case OPCODE_MOV_REG: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for MOV_REG at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t dst = program[pc++];
                uint8_t src = program[pc++];
                if (dst >= REGISTERS || src >= REGISTERS) {
                    fprintf(stderr, "Invalid register in MOV_REG at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                registers[dst] = registers[src];
                break;
            }
            case OPCODE_MOV_IMM: {
                if (pc + 5 >= size) {
                    fprintf(stderr, "Missing operands for MOV_IMM at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t dst = program[pc++];
                if (dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in MOV_IMM at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                int32_t value = program[pc] | (program[pc+1]<<8) | (program[pc+2]<<16) | (program[pc+3]<<24);
                pc += 4;
                registers[dst] = value;
                break;
            }
            case OPCODE_JMP: {
                if (pc + 3 >= size) {
                    fprintf(stderr, "Missing operand for JMP at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint32_t addr = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc = addr;
                if (pc >= size) {
                    fprintf(stderr, "JMP addr out of bounds: %lu at pc=%u\n", pc, addr);
                    free(program);
                    free(stack);
                    return 1;
                }
                break;
            }
            case OPCODE_NEWLINE: {
                putchar('\n'); 
                break;
            }
            case OPCODE_HALT: {
                free(program);
                free(stack);
                return 0;
            }
            case OPCODE_PRINT: {
                if (pc >= size) {
                    printf("Error: missing operand for PRINT at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t value = program[pc++];
                putchar(value);
                break;  
            }
            case OPCODE_ALLOC: {
                if (pc + 3 >= size) {
                    fprintf(stderr, "Missing operand for ALLOC at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }

                uint32_t elems = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc += 4;

                if (elems > stack_cap) {
                    int64_t *tmp = realloc(stack, elems * sizeof *stack);
                    if (!tmp) {
                        perror("realloc");
                        free(program);
                        free(stack);
                        return 1;
                    }
                    stack = tmp;
                    stack_cap = elems;
                }
                break;
            }
            case OPCODE_LOAD: {
                if (pc + 5 >= size) {
                    fprintf(stderr, "Missing operands for LOAD at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t reg = program[pc++];
                uint32_t addr = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc += 4;
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register in LOAD at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                if ((size_t)addr >= stack_cap) {
                    fprintf(stderr, "LOAD address out of bounds: %u at pc=%zu\n", addr, pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                registers[reg] = stack[addr];
                break;
            }
            case OPCODE_STORE: {
                if (pc + 5 >= size) {
                    fprintf(stderr, "Missing operands for STORE at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                uint8_t reg = program[pc++];
                uint32_t addr = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc += 4;
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register in STORE at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                if ((size_t)addr >= stack_cap) {
                    fprintf(stderr, "STORE address out of bounds: %u at pc=%zu\n", addr, pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                stack[addr] = registers[reg];
                break;
            }
            case OPCODE_GROW: {
                if (pc + 3 >= size) {
                    fprintf(stderr, "Missing operand for GROW at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }

                uint32_t elem = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc += 4;

                if (elem == 0) 
                    break;

                size_t new_cap = stack_cap + elem;

                int64_t *tmp = realloc(stack, new_cap * sizeof *stack);
                if (!tmp) {
                    perror("realloc");
                    free(program);
                    free(stack);
                    return 1;
                }
                stack = tmp;
                stack_cap = new_cap;
                break;
            }
            case OPCODE_PRINT_STACKSIZE: {
                printf("%zu", stack_cap);
                fflush(stdout);
                break;
            }
            case OPCODE_RESIZE: {
                if (pc + 3 >= size) {
                    fprintf(stderr, "Missing operand for RESIZE at pc=%zu\n", pc);
                    free(program);
                    free(stack);
                    return 1;
                }

                uint32_t new_size = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc += 4;

                int64_t *tmp = realloc(stack, new_size * sizeof *stack);
                if (!tmp) {
                    perror("realloc");
                    free(program);
                    free(stack);
                    return 1;
                }
                stack = tmp;
                stack_cap = new_size;
                if (sp > stack_cap) {
                    sp = stack_cap; 
                }
                break;
            }
            default: {
                fprintf(stderr, "Unknown opcode 0x%02X at position %zu\n", opcode, pc - 1);
                free(program);
                free(stack);
                return 1;
            }
        }
    }

    free(program);
    free(stack);
    return 0;
}