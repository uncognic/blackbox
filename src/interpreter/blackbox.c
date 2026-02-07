#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../define.h"
#include "../assembler/tools.h"
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#endif

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: bbx program.bcx\n");
        return 1;
    }
    srand((unsigned int)time(NULL) ^ (uintptr_t)&main);
    int64_t registers[REGISTERS] = {0};
    size_t sp = 0;
    size_t stack_cap = STACK_SIZE;
    int64_t *stack = NULL;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
    {
        perror("fopen");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *program = malloc(size);
    if (!program)
    {
        perror("malloc");
        fclose(f);
        return 1;
    }
    size_t n = fread(program, 1, size, f);
    if (n != size)
    {
        perror("fread");
        free(program);
        fclose(f);
        return 1;
    }
    if (size < 3)
    {
        fprintf(stderr, "Error: program too small (missing magic)\n");
        free(program);
        fclose(f);
        return 1;
    }
    uint8_t m0 = (MAGIC >> 16) & 0xFF;
    uint8_t m1 = (MAGIC >> 8) & 0xFF;
    uint8_t m2 = (MAGIC) & 0xFF;
    if (program[0] != m0 || program[1] != m1 || program[2] != m2)
    {
        fprintf(stderr, "Error: invalid magic (expected '%c%c%c')\n", m0, m1, m2);
        free(program);
        fclose(f);
        return 1;
    }

    if (size < 7)
    {
        fprintf(stderr, "Error: program too small (missing string table header)\n");
        free(program);
        fclose(f);
        return 1;
    }
    uint32_t string_table_size = (uint32_t)program[3] | ((uint32_t)program[4] << 8) | ((uint32_t)program[5] << 16) | ((uint32_t)program[6] << 24);
    uint8_t *string_table = &program[7];

    if (size < 7 + string_table_size)
    {
        fprintf(stderr, "Error: program too small for declared string table\n");
        free(program);
        fclose(f);
        return 1;
    }

    fclose(f);

    stack = malloc(stack_cap * sizeof *stack);
    if (!stack)
    {
        perror("malloc");
        free(program);
        return 1;
    }

    size_t pc = 7 + string_table_size;

    FILE *fds[FILE_DESCRIPTORS];
    for (size_t i = 0; i < FILE_DESCRIPTORS; i++)
    {
        fds[i] = NULL;
    }

    while (pc < size)
    {
        uint8_t opcode = program[pc++];
        switch (opcode)
        {
        case OPCODE_WRITE:
        {
            uint8_t fd = program[pc++];
            uint8_t len = program[pc++];
            if (fd != 1 && fd != 2)
            {
                fprintf(stderr, "Error: invalid fd %zu at pc=%u\n", pc, fd);
                free(program);
                free(stack);
                return 1;
            }
            if (pc + len > size)
            {
                fprintf(stderr, "Error: string past end of program at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            FILE *out = (fd == 1) ? stdout : stderr;
            size_t written = fwrite(&program[pc], 1, len, out);
            fflush(out);
            if (written != len)
            {
                perror("fwrite");
                free(program);
                free(stack);
                return 1;
            }
            pc += len;
            break;
        }
        case OPCODE_INC:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operand for INC at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in INC at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[reg] += 1;
            break;
        }
        case OPCODE_DEC:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operand for DEC at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in DEC at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[reg] -= 1;
            break;
        }
        case OPCODE_PUSH_IMM:
        {
            if (pc + 3 >= size)
            {
                fprintf(stderr, "Missing operand for PUSH at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            if (sp >= stack_cap)
            {
                size_t new_cap = stack_cap + stack_cap / 2;
                if (new_cap <= sp)
                    new_cap = sp + 1;
                int64_t *tmp = realloc(stack, new_cap * sizeof *stack);
                if (!tmp)
                {
                    perror("realloc");
                    free(program);
                    free(stack);
                    return 1;
                }
                stack = tmp;
                stack_cap = new_cap;
            }
            int32_t value = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;

            stack[sp++] = value;
            break;
        }
        case OPCODE_PUSH_REG:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operands for PUSH_REG at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t src = program[pc++];
            if (src >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in PUSH_REG at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            if (sp >= stack_cap)
            {
                size_t new_cap = stack_cap + stack_cap / 2;
                if (new_cap <= sp)
                    new_cap = sp + 1;
                int64_t *tmp = realloc(stack, new_cap * sizeof *stack);
                if (!tmp)
                {
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
        case OPCODE_CMP:
        {
            if (pc + 2 >= size)
            {
                fprintf(stderr, "Missing operands for CMP at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t src = program[pc++];
            uint8_t dst = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in CMP at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            int32_t result = registers[dst] - registers[src];
            if (result < 0)
            {
                registers[REGISTERS - 1] = 0;
            }
            else if (result > 0)
            {
                registers[REGISTERS - 1] = 1;
            }
            else
            {
                registers[REGISTERS - 1] = 0;
            }
            break;
        }
        case OPCODE_POP:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operand for POP at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register %u at pc=%zu\n", reg, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (sp == 0)
            {
                fprintf(stderr, "Stack underflow at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[reg] = stack[--sp];
            break;
        }
        case OPCODE_JZ:
        {
            if (pc + 4 >= size)
            {
                fprintf(stderr, "Missing operands for JZ at pc=%zu at pc=%zu\n", pc, pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t addr = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (registers[reg] == 0)
            {
                if (addr >= size)
                {
                    fprintf(stderr, "JZ address out of bounds: %u at pc=%zu\n", addr, pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                pc = addr;
            }
            break;
        }
        case OPCODE_JNZ:
        {
            if (pc + 4 >= size)
            {
                fprintf(stderr, "Missing operands for JNZ at pc=%zu at pc=%zu\n", pc, pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t addr = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (registers[reg] != 0)
            {
                if (addr >= size)
                {
                    fprintf(stderr, "JNZ address out of bounds: %u at pc=%zu\n", addr, pc);
                    free(program);
                    free(stack);
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
                fprintf(stderr, "Missing operands for ADD at pc=%zu at pc=%zu\n", pc, pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in ADD at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[dst] += registers[src];
            break;
        }
        case OPCODE_SUB:
        {
            if (pc + 2 >= size)
            {
                fprintf(stderr, "Missing operands for ADD at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in ADD at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[dst] -= registers[src];
            break;
        }
        case OPCODE_MUL:
        {
            if (pc + 2 >= size)
            {
                fprintf(stderr, "Missing operands for MUL at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in MUL at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[dst] *= registers[src];
            break;
        }
        case OPCODE_DIV:
        {
            if (pc + 2 >= size)
            {
                fprintf(stderr, "Missing operands for DIV at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (src >= REGISTERS || dst >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in DIV at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            if (registers[src] == 0)
            {
                fprintf(stderr, "Invalid: division by zero at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[dst] /= registers[src];
            break;
        }

        case OPCODE_PRINTREG:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operand for PRINTREG at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }

            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register");
                free(program);
                free(stack);
                return 1;
            }
            printf("%lld", (long long)registers[reg]);
            fflush(stdout);
            break;
        }
        case OPCODE_MOV_REG:
        {
            if (pc + 2 >= size)
            {
                fprintf(stderr, "Missing operands for MOV_REG at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in MOV_REG at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[dst] = registers[src];
            break;
        }
        case OPCODE_MOV_IMM:
        {
            if (pc + 5 >= size)
            {
                fprintf(stderr, "Missing operands for MOV_IMM at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t dst = program[pc++];
            if (dst >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in MOV_IMM at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            int32_t value = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            registers[dst] = value;
            break;
        }
        case OPCODE_JMP:
        {
            if (pc + 3 >= size)
            {
                fprintf(stderr, "Missing operand for JMP at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint32_t addr = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc = addr;
            if (pc >= size)
            {
                fprintf(stderr, "JMP addr out of bounds: %zu at pc=%u\n", pc, addr);
                free(program);
                free(stack);
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
            free(program);
            free(stack);
            return 0;
        }
        case OPCODE_PRINT:
        {
            if (pc >= size)
            {
                printf("Error: missing operand for PRINT at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t value = program[pc++];
            putchar(value);
            break;
        }
        case OPCODE_ALLOC:
        {
            if (pc + 3 >= size)
            {
                fprintf(stderr, "Missing operand for ALLOC at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }

            uint32_t elems = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;

            if (elems > stack_cap)
            {
                int64_t *tmp = realloc(stack, elems * sizeof *stack);
                if (!tmp)
                {
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
        case OPCODE_LOAD:
        {
            if (pc + 5 >= size)
            {
                fprintf(stderr, "Missing operands for LOAD at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t addr = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in LOAD at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            if ((size_t)addr >= stack_cap)
            {
                fprintf(stderr, "LOAD address out of bounds: %u at pc=%zu\n", addr, pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[reg] = stack[addr];
            break;
        }
        case OPCODE_STORE:
        {
            if (pc + 5 >= size)
            {
                fprintf(stderr, "Missing operands for STORE at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t addr = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in STORE at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            if ((size_t)addr >= stack_cap)
            {
                fprintf(stderr, "STORE address out of bounds: %u at pc=%zu\n", addr, pc);
                free(program);
                free(stack);
                return 1;
            }
            stack[addr] = registers[reg];
            break;
        }
        case OPCODE_GROW:
        {
            if (pc + 3 >= size)
            {
                fprintf(stderr, "Missing operand for GROW at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }

            uint32_t elem = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;

            if (elem == 0)
                break;

            size_t new_cap = stack_cap + elem;

            int64_t *tmp = realloc(stack, new_cap * sizeof *stack);
            if (!tmp)
            {
                perror("realloc");
                free(program);
                free(stack);
                return 1;
            }
            stack = tmp;
            stack_cap = new_cap;
            break;
        }
        case OPCODE_PRINT_STACKSIZE:
        {
            printf("%zu", stack_cap);
            fflush(stdout);
            break;
        }
        case OPCODE_RESIZE:
        {
            if (pc + 3 >= size)
            {
                fprintf(stderr, "Missing operand for RESIZE at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }

            uint32_t new_size = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;

            int64_t *tmp = realloc(stack, new_size * sizeof *stack);
            if (!tmp)
            {
                perror("realloc");
                free(program);
                free(stack);
                return 1;
            }
            stack = tmp;
            stack_cap = new_size;
            if (sp > stack_cap)
            {
                sp = stack_cap;
            }
            break;
        }
        case OPCODE_FREE:
        {
            if (pc + 3 >= size)
            {
                fprintf(stderr, "Missing operand for FREE at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }

            uint32_t num = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;

            if (num == 0)
                break;

            if (num > stack_cap)
            {
                fprintf(stderr, "FREE size out of bounds: %u at pc=%zu\n", num, pc);
                free(program);
                free(stack);
                return 1;
            }

            size_t new_cap = stack_cap - num;

            int64_t *tmp = realloc(stack, new_cap * sizeof *stack);
            if (!tmp)
            {
                perror("realloc");
                free(program);
                free(stack);
                return 1;
            }
            stack = tmp;
            stack_cap = new_cap;
            break;
        }
        case OPCODE_FOPEN:
        {
            if (pc + 2 >= size)
            {
                fprintf(stderr, "Missing operands for FOPEN at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t mode_str = program[pc++];
            uint8_t fd = program[pc++];
            uint8_t fname_len = program[pc++];

            if (fd >= FILE_DESCRIPTORS)
            {
                fprintf(stderr, "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (fname_len == 0 || fname_len >= 255)
            {
                fprintf(stderr, "Invalid filename length %u at pc=%zu\n", fname_len, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (pc + fname_len > size)
            {
                fprintf(stderr, "Filename past end of program at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }

            char fname[256];
            memcpy(fname, &program[pc], fname_len);
            fname[fname_len] = '\0';
            pc += fname_len;

            const char *mode;
            if (mode_str == 0)
                mode = "r";
            else if (mode_str == 1)
                mode = "w";
            else if (mode_str == 2)
                mode = "a";
            else
            {
                fprintf(stderr, "Invalid mode %u at pc=%zu\n", mode_str, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (fds[fd])
            {
                fclose(fds[fd]);
                fds[fd] = NULL;
            }

            FILE *file = fopen(fname, mode);
            if (!file)
            {
                perror("fopen");
                free(program);
                free(stack);
                return 1;
            }
            fds[fd] = file;
            break;
        }
        case OPCODE_FCLOSE:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operand for FCLOSE at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t fd = program[pc++];
            if (fd >= FILE_DESCRIPTORS)
            {
                fprintf(stderr, "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (fds[fd])
            {
                fclose(fds[fd]);
                fds[fd] = NULL;
            }
            break;
        }
        case OPCODE_FREAD:
        {
            if (pc + 1 >= size)
            {
                fprintf(stderr, "Missing operands for FREAD at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            size_t operand_pc = pc;
            uint8_t fd = program[pc++];
            uint8_t reg = program[pc++];
            if (fd >= FILE_DESCRIPTORS)
            {
                fprintf(stderr, "Invalid file descriptor %u at pc=%zu\n", fd, operand_pc);
                free(program);
                free(stack);
                return 1;
            }
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register %u at pc=%zu\n", reg, operand_pc);
                free(program);
                free(stack);
                return 1;
            }
            if (!fds[fd])
            {
                fprintf(stderr, "File descriptor %u not opened at pc=%zu\n", fd, operand_pc);
                free(program);
                free(stack);
                return 1;
            }
            int c = fgetc(fds[fd]);
            if (c == EOF)
            {
                if (feof(fds[fd]))
                {
                    registers[reg] = -1;
                }
                else
                {
                    perror("fgetc");
                    free(program);
                    free(stack);
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
                fprintf(stderr, "Missing operands for FWRITE_REG at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t fd = program[pc++];
            uint8_t reg = program[pc++];
            if (fd >= FILE_DESCRIPTORS)
            {
                fprintf(stderr, "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register %u at pc=%zu\n", reg, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (!fds[fd])
            {
                fprintf(stderr, "File descriptor %u not opened at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            int val = (int)registers[reg];
            if (fputc(val, fds[fd]) == EOF)
            {
                perror("fputc");
                free(program);
                free(stack);
                return 1;
            }
            break;
        }
        case OPCODE_FWRITE_IMM:
        {
            if (pc + 4 >= size)
            {
                fprintf(stderr, "Missing operands for FWRITE_IMM at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t fd = program[pc++];
            int32_t value = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (fd >= FILE_DESCRIPTORS)
            {
                fprintf(stderr, "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (!fds[fd])
            {
                fprintf(stderr, "File descriptor %u not opened at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (fputc(value, fds[fd]) == EOF)
            {
                perror("fputc");
                free(program);
                free(stack);
                return 1;
            }
            break;
        }
        case OPCODE_FSEEK_REG:
        {
            if (pc + 1 >= size)
            {
                fprintf(stderr, "Missing operands for FSEEK_REG at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t fd = program[pc++];
            uint8_t reg = program[pc++];
            if (fd >= FILE_DESCRIPTORS)
            {
                fprintf(stderr, "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register %u at pc=%zu\n", reg, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (!fds[fd])
            {
                fprintf(stderr, "File descriptor %u not opened at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (fseek(fds[fd], registers[reg], SEEK_SET) != 0)
            {
                perror("fseek");
                free(program);
                free(stack);
                return 1;
            }
            break;
        }
        case OPCODE_FSEEK_IMM:
        {
            if (pc + 3 >= size)
            {
                fprintf(stderr, "Missing operands for FSEEK_IMM at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t fd = program[pc++];
            int32_t offset = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (fd >= FILE_DESCRIPTORS)
            {
                fprintf(stderr, "Invalid file descriptor %u at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (!fds[fd])
            {
                fprintf(stderr, "File descriptor %u not opened at pc=%zu\n", fd, pc);
                free(program);
                free(stack);
                return 1;
            }
            if (fseek(fds[fd], offset, SEEK_SET) != 0)
            {
                perror("fseek");
                free(program);
                free(stack);
                return 1;
            }
            break;
        }
        case OPCODE_LOADSTR:
        {
            if (pc + 4 >= size)
            {
                fprintf(stderr, "Missing operands for LOADSTR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            uint32_t offset = program[pc] | (program[pc + 1] << 8) |
                              (program[pc + 2] << 16) | (program[pc + 3] << 24);
            pc += 4;
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in LOADSTR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            if (offset >= string_table_size)
            {
                fprintf(stderr, "String offset out of bounds: %u at pc=%zu\n", offset, pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[reg] = (int64_t)offset;
            break;
        }
        case OPCODE_PRINTSTR:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operand for PRINTSTR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in PRINTSTR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }

            uint32_t addr = (uint32_t)registers[reg];

            if (addr & 0x80000000)
            {
                uint32_t idx = addr & 0x7FFFFFFF;
                while (idx < sp && stack[idx] != 0)
                {
                    putchar((int)stack[idx++]);
                }
            }
            else
            {
                if (addr >= string_table_size)
                {
                    fprintf(stderr, "String offset out of bounds: %u at pc=%zu\n", addr, pc);
                    free(program);
                    free(stack);
                    return 1;
                }
                printf("%s", (char *)(string_table + addr));
            }
            fflush(stdout);
            break;
        }
        case OPCODE_NOT:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operand for NOT at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in NOT at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[reg] = ~registers[reg];
            break;
        }
        case OPCODE_AND:
        {
            if (pc + 1 >= size)
            {
                fprintf(stderr, "Missing operands for AND at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in AND at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[dst] = registers[dst] & registers[src];
            break;
        }
        case OPCODE_OR:
        {
            if (pc + 1 >= size)
            {
                fprintf(stderr, "Missing operands for OR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in OR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[dst] = registers[dst] | registers[src];
            break;
        }
        case OPCODE_XOR:
        {
            if (pc + 1 >= size)
            {
                fprintf(stderr, "Missing operands for XOR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t dst = program[pc++];
            uint8_t src = program[pc++];
            if (dst >= REGISTERS || src >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in XOR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            registers[dst] = registers[dst] ^ registers[src];
            break;
        }
        case OPCODE_READSTR:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operand for READSTR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in READSTR at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }

            uint32_t start_addr = sp | 0x80000000;

            int c;
            while ((c = getchar()) != EOF && c != '\n')
            {
                if (sp >= stack_cap)
                {
                    size_t new_cap = stack_cap + stack_cap / 2;
                    int64_t *tmp = realloc(stack, new_cap * sizeof *stack);
                    if (!tmp)
                    {
                        perror("realloc");
                        free(program);
                        free(stack);
                        return 1;
                    }
                    stack = tmp;
                    stack_cap = new_cap;
                }
                stack[sp++] = c;
            }
            if (sp >= stack_cap)
            {
                size_t new_cap = stack_cap + stack_cap / 2;
                int64_t *tmp = realloc(stack, new_cap * sizeof *stack);
                if (!tmp)
                {
                    perror("realloc");
                    free(program);
                    free(stack);
                    return 1;
                }
                stack = tmp;
                stack_cap = new_cap;
            }
            stack[sp++] = 0;

            registers[reg] = start_addr;
            break;
        }
        case OPCODE_SLEEP:
        {
            if (pc + 3 >= size)
            {
                fprintf(stderr, "Missing operand for SLEEP at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint32_t ms = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
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
        case OPCODE_CLRSCR:
        {
#ifdef _WIN32
            system("cls");
#else
            fputs("\x1b[2J\x1b[H", stdout);
            fflush(stdout);
#endif
            break;
        }
        case OPCODE_RAND:
        {
            if (pc >= size)
            {
                fprintf(stderr, "Missing operand for RAND at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in RAND at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            if (pc + 8 <= size)
            {
                int32_t min = program[pc] | (program[pc + 1] << 8) | (program[pc + 2] << 16) | (program[pc + 3] << 24);
                int32_t max = program[pc + 4] | (program[pc + 5] << 8) | (program[pc + 6] << 16) | (program[pc + 7] << 24);
                pc += 8;
                uint64_t r = get_true_random();
                int64_t lo = (int64_t)min;
                int64_t hi = (int64_t)max;
                if (lo > hi)
                {
                    int64_t t = lo;
                    lo = hi;
                    hi = t;
                }
                uint64_t range;
                if ((uint64_t)(hi - lo) == UINT64_MAX)
                {
                    registers[reg] = (int64_t)r;
                }
                else
                {
                    range = (uint64_t)(hi - lo) + 1;
                    if (range == 0)
                        registers[reg] = (int64_t)r;
                    else
                        registers[reg] = lo + (int64_t)(r % range);
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
                fprintf(stderr, "Missing operand for GETKEY at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in GETKEY at pc=%zu\n", pc);
                free(program);
                free(stack);
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
                fprintf(stderr, "Missing operand for READ at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }
            uint8_t reg = program[pc++];
            if (reg >= REGISTERS)
            {
                fprintf(stderr, "Invalid register in READ at pc=%zu\n", pc);
                free(program);
                free(stack);
                return 1;
            }

            long long v;
            if (scanf("%lld", &v) != 1)
                registers[reg] = 0;
            else
                registers[reg] = (int64_t)v;
            break;
        }
        default:
        {
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