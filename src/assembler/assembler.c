#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../interpreter/opcodes.h"

#define BCX_VERSION 1

char *trim(char *s) {
    while (isspace(*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && (isspace(*end) || *end == '\r' || *end == '\n')) *end-- = '\0';
    return s;
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

    fprintf(stderr, "Invalid register on line %d\n", lineno);
    exit(1);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s input.bbx output.bcx\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        perror("fopen input");
        return 1;
    }

    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        perror("fopen output");
        fclose(in);
        return 1;
    }
    

    char line[256];
    int lineno = 0;

    while (fgets(line, sizeof(line), in)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == ';')
            continue;
        
        if (strncmp(s, "WRITE", 5) == 0) {
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
            fputc(OPCODE_NEWLINE, out);
        }

        else if (strcmp(s, "HALT") == 0) {
            fputc(OPCODE_HALT, out);
        }
        else if (strncmp(s, "PRINT_REG", 9) == 0) {
            char regname[8];

            if (sscanf(s + 9, " %7s", regname) != 1) {
                fprintf(stderr,
                    "Syntax error on line %d: expected PRINT_REG <register>\n",
                    lineno
                );
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
            uint8_t reg;
            if      (strcmp(regname, "R0") == 0) reg = 0;
            else if (strcmp(regname, "R1") == 0) reg = 1;
            else if (strcmp(regname, "R2") == 0) reg = 2;
            else if (strcmp(regname, "R3") == 0) reg = 3;
            else if (strcmp(regname, "R4") == 0) reg = 4;
            else if (strcmp(regname, "R5") == 0) reg = 5;
            else if (strcmp(regname, "R6") == 0) reg = 6;
            else if (strcmp(regname, "R7") == 0) reg = 7;
            else {
                fprintf(stderr, "Invalid register on line %d\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }

            fputc(OPCODE_PRINTREG, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "PRINT", 5) == 0) {
            char c;
            if (sscanf(s + 5, " '%c", &c) != 1) {
                fprintf(stderr, "Syntax error on line %d\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_PRINT, out);
            fputc((uint8_t)c, out);
        }
        else if (strncmp(s, "PUSH", 4) == 0) {
            int32_t value;
            if (sscanf(s+4, " %d", &value) != 1) {
                fprintf(stderr, "Syntax error on line %d: expected PUSH <value>\n", lineno);
                fclose(in);
                fclose(out);
                return 1;   
            }
            fputc(OPCODE_PUSH, out);
            fputc((value >> 0) & 0xFF, out);
            fputc((value >> 8) & 0xFF, out);
            fputc((value >> 16) & 0xFF, out);
            fputc((value >> 24) & 0xFF, out);
        } 
        else if (strncmp(s, "POP", 3) == 0) {
            char regname[3];
            if (sscanf(s + 3, " %2s", regname) != 1) {
                fprintf(stderr, "Syntax error on line %d: expected POP <register>\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_POP, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "ADD", 3) == 0) {
            char src_reg[3];
            char dst_reg[3];
            if (sscanf(s + 3, " %2s , %2s", dst_reg, src_reg) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected ADD <src>, <dst>\n", lineno);
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
            char src_reg[3];
            char dst_reg[3];
            if (sscanf(s + 3, " %2s , %2s", dst_reg, src_reg) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected SUB <dst>, <src>\n", lineno);
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
            char src_reg[3];
            char dst_reg[3];
            if (sscanf(s + 3, " %2s , %2s", dst_reg, src_reg) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected MUL <dst>, <src>\n", lineno);
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
            char src_reg[3];
            char dst_reg[3];
            if (sscanf(s + 3, " %2s , %2s", dst_reg, src_reg) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected DIV <dst>, <src>\n", lineno);
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
            char dst_reg[3];
            char src[32];

            if (sscanf(s + 3, " %2s , %31s", dst_reg, src) != 2) {
                fprintf(stderr, "Syntax error on line %d: expected MOV <dst>, <src>\n", lineno);
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
                fputc(imm & 0xFF, out);
                fputc((imm >> 8) & 0xFF, out);
                fputc((imm >> 16) & 0xFF, out);
                fputc((imm >> 24) & 0xFF, out);
            }
        }
        
        else {
            fprintf(stderr, "Unknown instruction on line %d: %s\n", lineno, s);
            fclose(in);
            fclose(out);
            return 1;
        }

    }

    fclose(in);
    fclose(out);
    return 0;
}