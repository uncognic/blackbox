#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "../opcodes.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: bbx program.bcx");
        return 1;
    }
    int32_t registers[REGISTERS] = {0};
    int32_t stack[STACK_SIZE];
    size_t sp = 0;  
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
    fclose(f);

    size_t pc = 0;

    while (pc < size) {
        uint8_t opcode = program[pc++];
        switch (opcode) {
            case OPCODE_WRITE: {
                uint8_t fd = program[pc++];
                uint8_t len = program[pc++];
                if (fd != 1 && fd != 2) {
                    fprintf(stderr, "Error: invalid fd %lu at pc=%u\n", pc, fd);
                    free(program);
                    return 1;
                }
                if (pc + len > size) {
                    fprintf(stderr, "Error: string past end of program at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                if (write(fd, &program[pc], len) != len) {
                    perror("write");
                    free(program);
                    return 1;
                }
                pc += len;
                break;
            }
            case OPCODE_INC: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for INC at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t reg = program[pc++];
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register in INC at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                registers[reg] += 1;
                break;
            }
            case OPCODE_DEC: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for DEC at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t reg = program[pc++];
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register in DEC at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                registers[reg] -= 1;
                break;
            }
            case OPCODE_PUSH_IMM: {
                if (pc + 3 >= size) {
                    fprintf(stderr, "Missing operand for PUSH at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                if (sp >= STACK_SIZE) {
                    fprintf(stderr, "Stack overflow at pc=%zu\n", pc);
                    free(program);
                    return 1;
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
                    return 1;
                }
                uint8_t src = program[pc++];
                if (src >= REGISTERS) {
                    fprintf(stderr, "Invalid register in PUSH_REG at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                if (sp >= STACK_SIZE) {
                    fprintf(stderr, "Stack overflow at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                stack[sp++] = registers[src];
                break;
            }
            case OPCODE_CMP: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for CMP at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in CMP at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                int32_t result = registers[dst] - registers[src];
                if (result < 0) { 
                    registers[8] = 0;
                } else if (result > 0) {
                    registers[8] = 1;
                } else {
                    registers[8] = 0;
                }
                break;
            }
            case OPCODE_POP: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for POP at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t reg = program[pc++];
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register %u at pc=%zu\n", reg, pc);
                    free(program);
                    return 1;
                }
                if (sp == 0) {
                    fprintf(stderr, "Stack underflow at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                registers[reg] = stack[--sp];
                break;
            }
            case OPCODE_JZ: {
                if (pc + 4 >= size) {
                    fprintf(stderr, "Missing operands for JZ at pc=%zu at pc=%zu\n", pc, pc);
                    free(program);
                    return 1;
                }
                uint8_t reg = program[pc++];
                uint32_t addr = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc += 4;
                if (registers[reg] == 0) {
                    if (addr >= size) {
                        fprintf(stderr, "JZ address out of bounds: %u at pc=%zu\n", addr, pc);
                        free(program);
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
                    return 1;
                }
                uint8_t reg = program[pc++];
                uint32_t addr = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc += 4;
                if (registers[reg] != 0) {
                    if (addr >= size) {
                        fprintf(stderr, "JNZ address out of bounds: %u at pc=%zu\n", addr, pc);
                        free(program);
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
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in ADD at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                registers[dst] += registers[src];
                break;
            }
            case OPCODE_SUB: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for ADD at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in ADD at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                registers[dst] -= registers[src];
                break;
            }
            case OPCODE_MUL: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for MUL at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in MUL at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                registers[dst] *= registers[src];
                break;
            }
            case OPCODE_DIV: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for DIV at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in DIV at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                if (registers[src] == 0) {
                    fprintf(stderr, "Invalid: division by zero at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                registers[dst] /= registers[src];
                break;
            }

            case OPCODE_PRINTREG: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for PRINT_REG at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }

                uint8_t reg = program[pc++];
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register");
                    free(program);
                    return 1;
                }
                printf("%d", registers[reg]);
                fflush(stdout);
                break;
            }
            case OPCODE_MOV_REG: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for MOV_REG at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t dst = program[pc++];
                uint8_t src = program[pc++];
                if (dst >= REGISTERS || src >= REGISTERS) {
                    fprintf(stderr, "Invalid register in MOV_REG at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                registers[dst] = registers[src];
                break;
            }
            case OPCODE_MOV_IMM: {
                if (pc + 5 >= size) {
                    fprintf(stderr, "Missing operands for MOV_IMM at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t dst = program[pc++];
                if (dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in MOV_IMM at pc=%zu\n", pc);
                    free(program);
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
                    return 1;
                }
                uint32_t addr = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc = addr;
                if (pc >= size) {
                    fprintf(stderr, "JMP addr out of bounds: %lu at pc=%u\n", pc, addr);
                    free(program);
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
                return 0;
            }
            case OPCODE_PRINT: {
                if (pc >= size) {
                    printf("Error: missing operand for PRINT at pc=%zu\n", pc);
                    free(program);
                    return 1;
                }
                uint8_t value = program[pc++];
                putchar(value);
                break;  
            }
            default: {
                fprintf(stderr, "Unknown opcode 0x%02X at position %zu\n", opcode, pc - 1);
                free(program);
                return 1;
            }
        }
    }

    free(program);
    return 0;
}