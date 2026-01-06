#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../opcodes.h"

#define BCX_VERSION 1

char *trim(char *s) {
    while (isspace(*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && (isspace(*end) || *end == '\r' || *end == '\n')) *end-- = '\0';
    return s;
}
typedef struct {
    char name[32];
    size_t addr;
} Label;

size_t find_label(const char *name, Label *labels, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(labels[i].name, name) == 0) {
            return labels[i].addr;
        }
    }
    fprintf(stderr, "Unknown label %s\n", name);
    exit(1);
}
void write_u32(FILE *out, uint32_t val) {
    fputc((val >> 0) & 0xFF, out);
    fputc((val >> 8) & 0xFF, out);
    fputc((val >> 16) & 0xFF, out);
    fputc((val >> 24) & 0xFF, out);
}
size_t instr_size(const char *line) {
    if (strncmp(line, "MOV", 3) == 0) {
        char dst[8], src[32];

        const char *p = line + 3;
        while (*p && isspace(*p)) p++;

        const char *comma = strchr(p, ',');
        if (!comma) return 0;

        size_t len = comma - p;
        if (len >= sizeof(dst)) len = sizeof(dst) - 1;
        strncpy(dst, p, len);
        dst[len] = 0;

        p = comma + 1;
        while (*p && isspace(*p)) p++;
        strncpy(src, p, sizeof(src) - 1);
        src[sizeof(src)-1] = 0;

        char *end = src + strlen(src) - 1;
        while (end >= src && isspace(*end)) *end-- = 0;

        if (toupper(src[0]) == 'R') return 3; 
        else return 6;   
    }
    else if (strncmp(line, "PUSH", 4) == 0) {
        char operand[32];
        sscanf(line + 4, " %31s", operand);
        if (operand[0] == 'R') {
            return 2;
        } else {
            return 5;
        }
    }
    else if (strncmp(line, "POP", 3) == 0) return 2; 
    else if (strncmp(line, "ADD", 3) == 0) return 3;  
    else if (strncmp(line, "SUB", 3) == 0) return 3;
    else if (strncmp(line, "MUL", 3) == 0) return 3;
    else if (strncmp(line, "DIV", 3) == 0) return 3;
    else if (strncmp(line, "PRINT_REG", 9) == 0) return 2; 
    else if (strncmp(line, "PRINT", 5) == 0) return 2;
    else if (strncmp(line, "WRITE", 5) == 0) {
        char *quote = strchr(line, '"');
        if (!quote) return 3; 
        char *end = strchr(quote+1, '"');
        if (!end) return 3;
        size_t str_len = end - (quote+1);
        return 3 + str_len;
    }
    else if (strncmp(line, "JMP", 3) == 0) return 5; 
    else if (strcmp(line, "NEWLINE") == 0) return 1;
    else if (strncmp(line, "JZ", 2) == 0) return 6;
    else if (strncmp(line, "JNZ", 3) == 0) return 6;
    else if (strncmp(line, "INC", 3) == 0) return 2;
    else if (strncmp(line, "DEC", 3) == 0) return 2;  
    else if (strncmp(line, "CMP", 3) == 0) return 3;
    else if (strcmp(line, "HALT") == 0) return 1;
    return 0; 
}
uint8_t parse_register(const char *r, int lineno) {
    if (strcmp(r, "R0") == 0) return 0;
    if (strcmp(r, "R1") == 0) return 1;
    if (strcmp(r, "R2") == 0) return 2;
    if (strcmp(r, "R3") == 0) return 3;
    if (strcmp(r, "R4") == 0) return 4;
    if (strcmp(r, "R5") == 0) return 5;
    if (strcmp(r, "R6") == 0) return 6;
    if (strcmp(r, "R7") == 0) return 7;
    if (strcmp(r, "R8") == 0) return 8;

    fprintf(stderr, "Invalid register on line %d\n", lineno);
    exit(1);
}


int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        fprintf(stdout, "Usage: %s [-d, --debug] [-h, --help] input.bbx output.bcx\n", argv[0]);
        return 1;
    }
    char *input_file = NULL;
    char *output_file = NULL;
    int debug = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug = 1;
        } else if (!input_file) {
            input_file = argv[i];
        } else if (!output_file) {
            output_file = argv[i];
        } else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!input_file || !output_file) {
        fprintf(stderr, "Usage: %s [-d] input.bbx output.bcx\n", argv[0]);
        return 1;
    }
    
    FILE *in = fopen(input_file, "rb");
    if (!in) {
        perror("fopen input");
        return 1;
    }
    
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        perror("fopen output");
        fclose(in);
        return 1;
    }
    if (debug) {
        printf("Debug mode ON\n");
        printf("[DEBUG] Input file: %s\n", input_file);
        printf("[DEBUG] Output file: %s\n", output_file);
    }
    
    char line[256];
    int lineno = 0;
    Label labels[256];
    size_t label_count = 0;
    int pc = 0;

    while (fgets(line, sizeof(line), in)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == ';')
            continue;
        
        size_t len = strlen(s);
        if (s[0] == '.' && s[len-1] == ':') {
            if (label_count >= 256) {
                fprintf(stderr, "Too many labels (max 256)\n");
                fclose(in);
                fclose(out);
                return 1;
            }

            s[len-1] = '\0';
            strcpy(labels[label_count].name, s + 1);
            labels[label_count].addr = pc;
            if (debug) {
                printf("[DEBUG] Label %s at pc=%zu\n", labels[label_count].name, labels[label_count].addr);
            }
            label_count++;
            continue;
        } 
        
        pc += instr_size(s);
        if (debug) {
            printf("[DEBUG] Instruction: %s, size: %lu bytes, next PC=%u\n", s, instr_size(s), pc);
        }
    }
    rewind(in);
    lineno = 0;
        
    while (fgets(line, sizeof(line), in)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == ';')
            continue;
        
        if (strncmp(s, "WRITE", 5) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            int fd;
            char *str_start;

            if (sscanf(s + 5, " %d", &fd) != 1) {
                fprintf(stderr, "Syntax error on line %d: expected WRITE <fd> \"<string>\"\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (fd != 1 && fd != 2) {
                fprintf(stderr, "Invalid file descriptor on line %d: %d (only 1=stdout, 2=stderr allowed)\n", lineno, fd);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start = strchr(s, '"');
            if (!str_start) {
                fprintf(stderr, "Syntax error on line %d: missing opening quote for string\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start++;

            char *str_end = strchr(str_start, '"');
            if (!str_end) {
                fprintf(stderr, "Syntax error on line %d: missing closing quote for string\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            size_t len = str_end - str_start;
            if (len > 255) len = 255;

            fputc(OPCODE_WRITE, out);
            fputc((uint8_t)fd, out);
            fputc((uint8_t)len, out);

            for (size_t i = 0; i < len; i++) {
                fputc((uint8_t)str_start[i], out);
            }
        }

        else if (strcmp(s, "NEWLINE") == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_NEWLINE, out);
        }
        else if (s[0] == '.') {
            continue;
        }
        else if (strncmp(s, "JZ", 2) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[3];
            char label[32];
            if (sscanf(s + 2, " %2s, %31s", regname, label) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected JZ <register>, <label>\nGot: %s\n", lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t src = parse_register(regname, lineno);
            size_t addr = find_label(label, labels, label_count);
            if (debug) {
                printf("[DEBUG] JZ to %s (addr=%zu)\n", label, addr);
            }
            fputc(OPCODE_JZ, out);
            fputc(src, out);
            write_u32(out, addr);
        }
        else if (strncmp(s, "JNZ", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[3];
            char label[32];
            if (sscanf(s + 3, " %2s, %31s", regname, label) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected JNZ <register>, <label>\nGot: %s\n", lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t src = parse_register(regname, lineno);
            size_t addr = find_label(label, labels, label_count);
            if (debug) {
                printf("[DEBUG] JNZ to %s (addr=%zu)\n", label, addr);
            }
            fputc(OPCODE_JNZ, out);
            fputc(src, out);
            write_u32(out, addr);
        }

        else if (strcmp(s, "HALT") == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_HALT, out);
        }
        else if (strncmp(s, "INC", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[8];

            if (sscanf(s + 3, " %7s", regname) != 1) {
                fprintf(stderr, "Syntax error on line %d: expected INC <register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_INC, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "DEC", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[8];

            if (sscanf(s + 3, " %7s", regname) != 1) {
                fprintf(stderr, "Syntax error on line %d: expected DEC <register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_DEC, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "PRINT_REG", 9) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[8];

            if (sscanf(s + 9, " %7s", regname) != 1) {
                fprintf(stderr, "Syntax error on line %d: expected PRINT_REG <register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_PRINTREG, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "PRINT", 5) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char c;
            if (sscanf(s + 5, " '%c", &c) != 1) {
                fprintf(stderr, "Syntax error on line %d\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_PRINT, out);
            fputc((uint8_t)c, out);
        }
        else if (strncmp(s, "JMP", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label_name[32];
            if (sscanf(s+3, " %31s", label_name) == 0) {
                fprintf(stderr, "Syntax error on line %d: expected JMP <label>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            size_t addr = find_label(label_name, labels, label_count);
            if (debug) {
                printf("[DEBUG] JMP to %s (addr=%zu)\n", label_name, addr);
            }
            fputc(OPCODE_JMP, out);
            write_u32(out, addr);
        }
        else if (strncmp(s, "POP", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[3];
            if (sscanf(s + 3, " %2s", regname) != 1) {
                fprintf(stderr, "Syntax error on line %d: expected POP <register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_POP, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "ADD", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[3];
            char dst_reg[3];
            if (sscanf(s + 3, " %2s , %2s", dst_reg, src_reg) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected ADD <src>, <dst>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_ADD, out);
            fputc(src, out);
            fputc(dst, out);
        }
        else if (strncmp(s, "SUB", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[3];
            char dst_reg[3];
            if (sscanf(s + 3, " %2s , %2s", dst_reg, src_reg) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected SUB <dst>, <src>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_SUB, out);
            fputc(src, out);
            fputc(dst, out);
        }
        else if (strncmp(s, "MUL", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[3];
            char dst_reg[3];
            if (sscanf(s + 3, " %2s , %2s", dst_reg, src_reg) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected MUL <dst>, <src>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_MUL, out);
            fputc(src, out);
            fputc(dst, out);
        }
        else if (strncmp(s, "DIV", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[3];
            char dst_reg[3];
            if (sscanf(s + 3, " %2s , %2s", dst_reg, src_reg) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected DIV <dst>, <src>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_DIV, out);
            fputc(src, out);
            fputc(dst, out);
        }
        else if (strncmp(s, "MOV", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char dst_reg[3];
            char src[32];

            if (sscanf(s + 3, " %2s , %31s", dst_reg, src) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected MOV <dst>, <src>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            if (src[0] == 'R') {
                uint8_t srcp = parse_register(src, lineno);
                fputc(OPCODE_MOV_REG, out);
                fputc(dst, out);
                fputc(srcp, out);
            }
            else {
                int32_t imm = strtol(src, NULL, 0);
                fputc(OPCODE_MOV_IMM, out);
                fputc(dst, out);
                write_u32(out, imm);
            }
        }
        else if (strncmp(s, "PUSH", 4) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 4, " %31s", operand) != 1) {
                fprintf(stderr, "Syntax error on line %d: expected PUSH <value|register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (operand[0] == 'R') {
                uint8_t reg = parse_register(operand, lineno);
                if (debug) {
                    printf("[DEBUG] PUSH_REG\n");
                }   
                fputc(OPCODE_PUSH_REG, out);
                fputc(reg, out);
            } else {
                if (debug) {
                    printf("[DEBUG] PUSH_IMM\n");
                }
                char *end;
                int32_t imm = strtol(operand, &end, 0);
                if (*end != '\0') {
                    fprintf(stderr, "Invalid immediate on line %d: %s\n", lineno, operand);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                fputc(OPCODE_PUSH_IMM, out);
                write_u32(out, imm);
            }
        }
        else if (strncmp(s, "CMP", 3) == 0) {
            if (debug) {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char reg1[3];
            char reg2[3];
            if (sscanf(s + 3, " %2s , %2s", reg1, reg2) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected CMP <reg1>, <reg2>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t r1 = parse_register(reg1, lineno);
            uint8_t r2 = parse_register(reg2, lineno);
            fputc(OPCODE_CMP, out);
            fputc(r1, out);
            fputc(r2, out);
        }
        
        else {
            fprintf(stderr, "Unknown instruction on line %d:\n %s\n", lineno, s);
            fclose(in);
            fclose(out);
            return 1;
        }

    }

    fclose(in);
    fclose(out);
    printf("Assembly successful.\n");
    return 0;
}