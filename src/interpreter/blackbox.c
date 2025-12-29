#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "opcodes.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s program.bin\n", argv[0]);
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
                    fprintf(stderr, "Error: invalid fd %u\n", fd);
                    free(program);
                    return 1;
                }
                if (pc + len > size) {
                    fprintf(stderr, "Error: string past end of program\n");
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
            case OPCODE_PUSH: {
                if (pc + 3 >= size) {
                    fprintf(stderr, "Missing operand for PUSH\n");
                    free(program);
                    return 1;
                }
                if (sp >= STACK_SIZE) {
                    fprintf(stderr, "Stack overflow\n");
                    free(program);
                    return 1;
                }
                int32_t value = program[pc] | (program[pc+1]<<8) | (program[pc+2]<<16) | (program[pc+3]<<24);
                pc += 4;

                stack[sp++] = value;
                break;
            }
            case OPCODE_POP: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for POP\n");
                    free(program);
                    return 1;
                }
                uint8_t reg = program[pc++];
                if (reg >= REGISTERS) {
                    fprintf(stderr, "Invalid register %u\n", reg);
                    free(program);
                    return 1;
                }
                if (sp == 0) {
                    fprintf(stderr, "Stack underflow\n");
                    free(program);
                    return 1;
                }
                registers[reg] = stack[--sp];
                break;
            }
            case OPCODE_ADD: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for ADD\n");
                    free(program);
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in ADD\n");
                    free(program);
                    return 1;
                }
                registers[dst] += registers[src];
                break;
            }
            case OPCODE_SUB: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for ADD\n");
                    free(program);
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in ADD\n");
                    free(program);
                    return 1;
                }
                registers[dst] -= registers[src];
                break;
            }
            case OPCODE_MUL: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for MUL\n");
                    free(program);
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in MUL\n");
                    free(program);
                    return 1;
                }
                registers[dst] *= registers[src];
                break;
            }
            case OPCODE_DIV: {
                if (pc + 2 >= size) {
                    fprintf(stderr, "Missing operands for DIV\n");
                    free(program);
                    return 1;
                }
                uint8_t src = program[pc++];
                uint8_t dst = program[pc++];
                if (src >= REGISTERS || dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in DIV\n");
                    free(program);
                    return 1;
                }
                registers[dst] /= registers[src];
                break;
            }

            case OPCODE_PRINTREG: {
                if (pc >= size) {
                    fprintf(stderr, "Missing operand for PRINT_REG\n");
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
                    fprintf(stderr, "Missing operands for MOV_REG\n");
                    free(program);
                    return 1;
                }
                uint8_t dst = program[pc++];
                uint8_t src = program[pc++];
                if (dst >= REGISTERS || src >= REGISTERS) {
                    fprintf(stderr, "Invalid register in MOV_REG\n");
                    free(program);
                    return 1;
                }
                registers[dst] = registers[src];
                break;
            }
            case OPCODE_MOV_IMM: {
                if (pc + 5 >= size) {
                    fprintf(stderr, "Missing operands for MOV_IMM\n");
                    free(program);
                    return 1;
                }
                uint8_t dst = program[pc++];
                if (dst >= REGISTERS) {
                    fprintf(stderr, "Invalid register in MOV_IMM\n");
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
                    fprintf(stderr, "Missing operand for JMP\n");
                    free(program);
                    return 1;
                }
                uint32_t addr = program[pc] | (program[pc+1] << 8) | (program[pc+2] << 16) | (program[pc+3] << 24);
                pc = addr;
                if (pc >= size) {
                    fprintf(stderr, "JMP addr out of bounds: %u\n", addr);
                    free(program);
                    return 1;
                }
                break;
            }
            case OPCODE_NEWLINE:
                putchar('\n'); 
                break;
            
            case OPCODE_HALT:
                free(program);
                return 0;
            case OPCODE_PRINT: {
                if (pc >= size) {
                    printf("Error: missing operand for PRINT\n");
                    free(program);
                    return 1;
                }
                uint8_t value = program[pc++];
                putchar(value);
                break;
            }
            default:
                printf("Unknown opcode 0x%02X at position %ld\n", opcode, pc - 1);
                free(program);
                return 1;
        }
    }

    free(program);
    return 0;
}