#include "asm.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../define.h"
#include "tools.h"

int assemble_file(const char *filename, const char *output_file, int debug) {
    FILE *in = fopen(filename, "rb");
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

    char line[256];
    int lineno = 0;

    while (fgets(line, sizeof(line), in)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == ';') continue;
        if (strcmp(s, "%asm") == 0) {
            break; 
        } else {
            fprintf(stderr, "Error: file must start with %%asm (line %d)\n", lineno);
            fclose(in);
            fclose(out);
            return 1;
        }
    }

    Label labels[256];
    size_t label_count = 0;
    uint32_t pc = 3;

    String strings[256];
    size_t string_count = 0;
    uint32_t string_table_size = 0;
    char string_data[8192];

    int current_section = 0;
    int found_code_section = 0;

    while (fgets(line, sizeof(line), in))
    {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == ';')
            continue;

        if (strcmp(s, "%string") == 0 || strcmp(s, "%strings") == 0)
        {
            if (found_code_section)
            {
                fprintf(stderr, "Error on line %d: %%string section must come before %%main/%%entry\n", lineno);
                fclose(in); fclose(out);
                return 1;
            }
            current_section = 1;
            if (debug)
                printf("[DEBUG] Entering string section at line %d\n", lineno);
            continue;
        }
        if (strcmp(s, "%main") == 0 || strcmp(s, "%entry") == 0)
        {
            current_section = 2;
            found_code_section = 1;
            if (debug)
                printf("[DEBUG] Entering code section at line %d\n", lineno);
            continue;
        }

        if (strncmp(s, "STR ", 4) == 0)
        {
            if (current_section != 1)
            {
                fprintf(stderr, "Error on line %d: STR must be inside %%string section\n", lineno);
                fclose(in); fclose(out);
                return 1;
            }
            char name[32];
            char *quote_start = strchr(s, '"');
            if (!quote_start || sscanf(s + 4, " $%31[^,]", name) != 1)
            {
                fprintf(stderr, "Syntax error line %d: expected STR $name, \"value\"\n", lineno);
                fclose(in); fclose(out);
                return 1;
            }
            quote_start++;
            char *quote_end = strchr(quote_start, '"');
            if (!quote_end)
            {
                fprintf(stderr, "Syntax error line %d: missing closing quote\n", lineno);
                fclose(in); fclose(out);
                return 1;
            }
            size_t len = quote_end - quote_start;
            
            strncpy(strings[string_count].name, name, 31);
            strings[string_count].offset = string_table_size;
            
            memcpy(string_data + string_table_size, quote_start, len);
            string_data[string_table_size + len] = '\0'; 
            string_table_size += len + 1;
            string_count++;
            
            if (debug)
                printf("[DEBUG] String $%s at offset %u\n", name, strings[string_count-1].offset);
            continue;
        }

        if (current_section != 2)
        {
            size_t len = strlen(s);
            if (s[0] == '.' && s[len - 1] == ':')
            {
                fprintf(stderr, "Error on line %d: labels must be inside %%main/%%entry section\n", lineno);
                fclose(in); fclose(out);
                return 1;
            }
            if (current_section == 0)
            {
                fprintf(stderr, "Error on line %d: code outside of section. Use %%string or %%main/%%entry\n", lineno);
                fclose(in); fclose(out);
                return 1;
            }
            continue;
        }

        size_t len = strlen(s);
        if (s[0] == '.' && s[len - 1] == ':')
        {
            if (label_count >= 256)
            {
                fprintf(stderr, "Too many labels (max 256)\n");
                fclose(in);
                fclose(out);
                return 1;
            }

            s[len - 1] = '\0';
            strncpy(labels[label_count].name, s + 1, sizeof(labels[label_count].name) - 1);
            labels[label_count].name[sizeof(labels[label_count].name) - 1] = '\0';
            labels[label_count].addr = pc;
            if (debug)
            {
                printf("[DEBUG] Label %s at pc=%u\n", labels[label_count].name, (unsigned)labels[label_count].addr);
            }
            label_count++;
            continue;
        }

        pc += (uint32_t)instr_size(s);
    }

    if (!found_code_section)
    {
        fprintf(stderr, "Error: missing %main or %entry section\n");
        fclose(in);
        fclose(out);
        return 1;
    }
    
    uint32_t header_offset = 4 + string_table_size;
    for (size_t i = 0; i < label_count; i++)
    {
        labels[i].addr += header_offset;
    }
    
    rewind(in);
    lineno = 0;
    current_section = 0;

    fputc((MAGIC >> 16) & 0xFF, out);
    fputc((MAGIC >> 8) & 0xFF, out);
    fputc((MAGIC >> 0) & 0xFF, out);
    write_u32(out, string_table_size);
    fwrite(string_data, 1, string_table_size, out);

    while (fgets(line, sizeof(line), in))
    {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == ';')
            continue;
        if (strcmp(s, "%string") == 0 || strcmp(s, "%strings") == 0)
        {
            current_section = 1;
            continue;
        }
        if (strcmp(s, "%main") == 0 || strcmp(s, "%entry") == 0)
        {
            current_section = 2;
            continue;
        }

        if (current_section == 1)
            continue;

        if (current_section != 2)
            continue;

        if (strncmp(s, "STR ", 4) == 0)
            continue;
        else if (strncmp(s, "WRITE", 5) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            int fd;
            char *str_start;

            if (sscanf(s + 5, " %d", &fd) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected WRITE <fd> \"<string>\"\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (fd != 1 && fd != 2)
            {
                fprintf(stderr, "Invalid file descriptor on line %d: %d (only 1=stdout, 2=stderr allowed)\n", lineno, fd);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start = strchr(s, '"');
            if (!str_start)
            {
                fprintf(stderr, "Syntax error on line %d: missing opening quote for string\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start++;

            char *str_end = strchr(str_start, '"');
            if (!str_end)
            {
                fprintf(stderr, "Syntax error on line %d: missing closing quote for string\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            size_t len = str_end - str_start;
            if (len > 255)
                len = 255;

            fputc(OPCODE_WRITE, out);
            fputc((uint8_t)fd, out);
            fputc((uint8_t)len, out);

            for (size_t i = 0; i < len; i++)
            {
                fputc((uint8_t)str_start[i], out);
            }
        }
        else if (strncmp(s, "LOADSTR", 7) == 0)
        {
            char name[32];
            char regname[16];
            if (sscanf(s + 7, " $%31[^,], %15s", name, regname) != 2)
            {
                fprintf(stderr, "Syntax error line %d: expected LOADSTR $name, <register>\n", lineno);
                fclose(in); fclose(out);
                return 1;
            }
            uint32_t offset = find_string(name, strings, string_count);
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADSTR, out);
            fputc(reg, out);
            write_u32(out, offset);
            if (debug)
                printf("[DEBUG] LOADSTR $%s (offset=%u) -> %s\n", name, offset, regname);
        }
        else if (strncmp(s, "PRINT_STR", 9) == 0)
        {
            if (debug)
                printf("[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            if (sscanf(s + 9, " %15s", regname) != 1)
            {
                fprintf(stderr, "Syntax error line %d: expected PRINT_STR <register>\n", lineno);
                fclose(in); fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_PRINT_STR, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "CONTINUE", 8) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
        }
        else if (strcmp(s, "NEWLINE") == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_NEWLINE, out);
        }
        else if (s[0] == '.')
        {
            continue;
        }
        else if (strncmp(s, "JZ", 2) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            char label[32];
            if (sscanf(s + 2, " %3s, %31s", regname, label) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected JZ <register>, <label>\nGot: %s\n", lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t src = parse_register(regname, lineno);
            uint32_t addr = find_label(label, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JZ to %s (addr=%u)\n", label, (unsigned)addr);
            }
            fputc(OPCODE_JZ, out);
            fputc(src, out);
            write_u32(out, addr);
        }
        else if (strncmp(s, "JNZ", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            char label[32];
            if (sscanf(s + 3, " %3s, %31s", regname, label) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected JNZ <register>, <label>\nGot: %s\n", lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t src = parse_register(regname, lineno);
            uint32_t addr = find_label(label, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JNZ to %s (addr=%u)\n", label, (unsigned)addr);
            }
            fputc(OPCODE_JNZ, out);
            fputc(src, out);
            write_u32(out, addr);
        }

        else if (strcmp(s, "HALT") == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_HALT, out);
        }
        else if (strncmp(s, "INC", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 3, " %7s", regname) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected INC <register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++)
            {
                if (regname[i] == '\r' || regname[i] == '\n')
                {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_INC, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "DEC", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 3, " %7s", regname) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected DEC <register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++)
            {
                if (regname[i] == '\r' || regname[i] == '\n')
                {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_DEC, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "PRINT_REG", 9) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 9, " %7s", regname) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected PRINT_REG <register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++)
            {
                if (regname[i] == '\r' || regname[i] == '\n')
                {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_PRINTREG, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "PRINT_STACKSIZE", 15) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_PRINT_STACKSIZE, out);
        }
        else if (strncmp(s, "PRINT", 5) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char c;
            if (sscanf(s + 5, " '%c", &c) != 1)
            {
                fprintf(stderr, "Syntax error on line %d\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_PRINT, out);
            fputc((uint8_t)c, out);
        }
        else if (strncmp(s, "JMP", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label_name[32];
            if (sscanf(s + 3, " %31s", label_name) == 0)
            {
                fprintf(stderr, "Syntax error on line %d: expected JMP <label>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label_name, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JMP to %s (addr=%u)\n", label_name, (unsigned)addr);
            }
            fputc(OPCODE_JMP, out);
            write_u32(out, addr);
        }
        else if (strncmp(s, "POP", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            if (sscanf(s + 3, " %3s", regname) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected POP <register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_POP, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "ADD", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s , %3s", dst_reg, src_reg) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected ADD <src>, <dst>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_ADD, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (strncmp(s, "SUB", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s , %3s", dst_reg, src_reg) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected SUB <dst>, <src>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_SUB, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (strncmp(s, "MUL", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s, %3s", dst_reg, src_reg) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected MUL <dst>, <src>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_MUL, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (strncmp(s, "DIV", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s, %3s", dst_reg, src_reg) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected DIV <dst>, <src>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_DIV, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (strncmp(s, "MOV", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char dst_reg[16];
            char src[16];

            if (sscanf(s + 3, " %3s, %15s", dst_reg, src) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected MOV <dst>, <src>\nGot: %s\n(If you're using a 2 character register like R1 or R2, use R01 or R02 instead!)\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            if (src[0] == 'R')
            {
                uint8_t srcp = parse_register(src, lineno);
                fputc(OPCODE_MOV_REG, out);
                fputc(dst, out);
                fputc(srcp, out);
            }
            else
            {
                int32_t imm = strtol(src, NULL, 0);
                fputc(OPCODE_MOV_IMM, out);
                fputc(dst, out);
                write_u32(out, imm);
            }
        }
        else if (strncmp(s, "PUSH", 4) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[16];
            if (sscanf(s + 4, " %3s", operand) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected PUSH <value|register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (operand[0] == 'R')
            {
                uint8_t reg = parse_register(operand, lineno);
                if (debug)
                {
                    printf("[DEBUG] PUSH_REG\n");
                }
                fputc(OPCODE_PUSH_REG, out);
                fputc(reg, out);
            }
            else
            {
                if (debug)
                {
                    printf("[DEBUG] PUSH_IMM\n");
                }
                char *end;
                int32_t imm = strtol(operand, &end, 0);
                if (*end != '\0')
                {
                    fprintf(stderr, "Invalid immediate on line %d: %s\n", lineno, operand);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                fputc(OPCODE_PUSH_IMM, out);
                write_u32(out, imm);
            }
        }
        else if (strncmp(s, "CMP", 3) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %3s, %3s", reg1, reg2) != 2)
            {
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
        else if (strncmp(s, "ALLOC", 5) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 5, " %31s", operand) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected ALLOC <elements>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, NULL, 0);
            fputc(OPCODE_ALLOC, out);
            write_u32(out, num);
        }
        else if (strncmp(s, "LOAD", 4) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            char addrname[32];
            if (sscanf(s + 4, " %3s, %31s", regname, addrname) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected LOAD <register>, <index in stack>\nGot: %s\n", lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            uint32_t addr = strtoul(addrname, NULL, 0);
            fputc(OPCODE_LOAD, out);
            fputc(reg, out);
            write_u32(out, addr);
        }
        else if (strncmp(s, "STORE", 5) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            char addrname[32];
            if (sscanf(s + 5, " %3s, %31s", regname, addrname) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected STORE <register>, <index in stack>\nGot: %s\n", lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            uint32_t addr = strtoul(addrname, NULL, 0);
            fputc(OPCODE_STORE, out);
            fputc(reg, out);
            write_u32(out, addr);
        }
        else if (strncmp(s, "GROW", 4) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 4, " %31s", operand) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected GROW <additional elements>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, NULL, 0);
            fputc(OPCODE_GROW, out);
            write_u32(out, num);
        }
        else if (strncmp(s, "RESIZE", 6) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 6, " %31s", operand) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected RESIZE <new size>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, NULL, 0);
            fputc(OPCODE_RESIZE, out);
            write_u32(out, num);
        }
        else if (strncmp(s, "FREE", 4) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 4, " %31s", operand) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected FREE <number of elements>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, NULL, 0);
            fputc(OPCODE_FREE, out);
            write_u32(out, num);
        }
        else if (strncmp(s, "FOPEN", 5) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char filename[128];
            char mode_raw[8];
            char fid_raw[8];
            if (sscanf(s + 5, " %7[^,], %7[^,], \"%127[^\"]\"", mode_raw, fid_raw, filename) != 3)
            {
                fprintf(stderr, "Syntax error on line %d: expected FOPEN <mode>, <file_descriptor>, \"<filename>\"\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *mode = trim(mode_raw);
            char *fid = trim(fid_raw);
            uint8_t mode_flag;
            if (strcmp(mode, "r") == 0)
            {
                mode_flag = 0;
            }
            else if (strcmp(mode, "w") == 0)
            {
                mode_flag = 1;
            }
            else if (strcmp(mode, "a") == 0)
            {
                mode_flag = 2;
            }
            else
            {
                fprintf(stderr, "Invalid mode on line %d: %s (expected r, w, or a)\n", lineno, mode);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_FOPEN, out);
            fputc(mode_flag, out);
            uint8_t fd = parse_file(fid, lineno);
            fputc(fd, out);
            uint8_t fname_len = (uint8_t)strlen(filename);
            fputc(fname_len, out);
            for (uint8_t i = 0; i < fname_len; i++)
            {
                fputc((uint8_t)filename[i], out);
            }
        }
        else if (strncmp(s, "FCLOSE", 6) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char fid[4];
            if (sscanf(s + 6, " %3s", fid) != 1)
            {
                fprintf(stderr, "Syntax error on line %d: expected FCLOSE <file_descriptor>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t fd = parse_file(fid, lineno);
            fputc(OPCODE_FCLOSE, out);
            fputc(fd, out);
        }
        else if (strncmp(s, "FREAD", 5) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char fid_raw[8];
            char reg_raw[8];
            if (sscanf(s + 5, " %7[^,], %7[^,]", fid_raw, reg_raw) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected FREAD <file_descriptor>, <register to read into>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *fid = trim(fid_raw);
            char *reg_tok = trim(reg_raw);
            uint8_t fd = parse_file(fid, lineno);
            uint8_t reg = parse_register(reg_tok, lineno);
            fputc(OPCODE_FREAD, out);
            fputc(fd, out);
            fputc(reg, out);
        }
        else if (strncmp(s, "FSEEK", 5) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char fid_raw[8];
            char offset_raw[64];
            if (sscanf(s + 5, " %7[^,], %63[^\n]", fid_raw, offset_raw) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected FSEEK <file_descriptor>, <offset_value|offset_register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *fid = trim(fid_raw);
            char *offset_tok = trim(offset_raw);
            uint8_t fd = parse_file(fid, lineno);
            if (offset_tok[0] == 'R')
            {
                uint8_t offset_reg = parse_register(offset_tok, lineno);
                fputc(OPCODE_FSEEK_REG, out);
                fputc(fd, out);
                fputc(offset_reg, out);
            }
            else if (offset_tok[0] == '"')
            {
                char inner[64];
                if (sscanf(offset_tok, "\"%63[^\"]\"", inner) != 1)
                {
                    fprintf(stderr, "Syntax error on line %d: malformed quoted offset\nGot: %s\n", lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                int32_t offset_imm = (int32_t)strtol(inner, NULL, 0);
                fputc(OPCODE_FSEEK_IMM, out);
                fputc(fd, out);
                write_u32(out, offset_imm);
            }
            else
            {
                int32_t offset_imm = (int32_t)strtol(offset_tok, NULL, 0);
                fputc(OPCODE_FSEEK_IMM, out);
                fputc(fd, out);
                write_u32(out, offset_imm);
            }
        }
        else if (strncmp(s, "FWRITE", 6) == 0)
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char fid_raw[8];
            char size_raw[64];
            if (sscanf(s + 6, " %7[^,], %63[^\n]", fid_raw, size_raw) != 2)
            {
                fprintf(stderr, "Syntax error on line %d: expected FWRITE <file_descriptor>, <size_value|size_register>\nGot: %s\n", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *fid = trim(fid_raw);
            char *size_tok = trim(size_raw);
            uint8_t fd = parse_file(fid, lineno);
            if (size_tok[0] == 'R')
            {
                uint8_t size_reg = parse_register(size_tok, lineno);
                fputc(OPCODE_FWRITE_REG, out);
                fputc(fd, out);
                fputc(size_reg, out);
            }
            else if (size_tok[0] == '"')
            {
                char inner[64];
                if (sscanf(size_tok, "\"%63[^\"]\"", inner) != 1)
                {
                    fprintf(stderr, "Syntax error on line %d: malformed quoted size\nGot: %s\n", lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                uint32_t size_imm = strtoul(inner, NULL, 0);
                fputc(OPCODE_FWRITE_IMM, out);
                fputc(fd, out);
                write_u32(out, size_imm);
            }
            else
            {
                uint32_t size_imm = strtoul(size_tok, NULL, 0);
                fputc(OPCODE_FWRITE_IMM, out);
                fputc(fd, out);
                write_u32(out, size_imm);
            }
        }

        else
        {
            fprintf(stderr, "Unknown instruction on line %d:\n %s\n", lineno, s);
            fclose(in);
            fclose(out);
            return 1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}