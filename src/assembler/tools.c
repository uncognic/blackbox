#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tools.h"
#include "../define.h"

uint32_t find_label(const char *name, Label *labels, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp(labels[i].name, name) == 0)
        {
            return labels[i].addr;
        }
    }
    fprintf(stderr, "Unknown label %s\n", name);
    exit(1);
}
uint32_t find_string(const char *name, String *strings, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp(strings[i].name, name) == 0)
            return strings[i].offset;
    }
    fprintf(stderr, "Error: undefined string constant '%s'\n", name);
    exit(1);
}
void write_u32(FILE *out, uint32_t val)
{
    fputc((val >> 0) & 0xFF, out);
    fputc((val >> 8) & 0xFF, out);
    fputc((val >> 16) & 0xFF, out);
    fputc((val >> 24) & 0xFF, out);
}
size_t instr_size(const char *line)
{
    if (strncmp(line, "MOV", 3) == 0)
    {
        char dst[8], src[32];

        const char *p = line + 3;
        while (*p && isspace(*p))
            p++;

        const char *comma = strchr(p, ',');
        if (!comma)
            return 0;

        size_t len = comma - p;
        if (len >= sizeof(dst))
            len = sizeof(dst) - 1;
        strncpy(dst, p, len);
        dst[len] = 0;

        p = comma + 1;
        while (*p && isspace(*p))
            p++;
        strncpy(src, p, sizeof(src) - 1);
        src[sizeof(src) - 1] = 0;

        char *end = src + strlen(src) - 1;
        while (end >= src && isspace(*end))
            *end-- = 0;

        if (toupper(src[0]) == 'R')
            return 3;
        else
            return 6;
    }
    else if (strncmp(line, "PUSH", 4) == 0)
    {
        char operand[32];
        sscanf(line + 4, " %31s", operand);
        if (operand[0] == 'R')
        {
            return 2;
        }
        else
        {
            return 5;
        }
    }
    else if (strncmp(line, "POP", 3) == 0)
        return 2;
    else if (strncmp(line, "ADD", 3) == 0)
        return 3;
    else if (strncmp(line, "SUB", 3) == 0)
        return 3;
    else if (strncmp(line, "MUL", 3) == 0)
        return 3;
    else if (strncmp(line, "DIV", 3) == 0)
        return 3;
    else if (strcmp(line, "PRINT_STACKSIZE") == 0)
        return 1;
    else if (strncmp(line, "PRINT_REG", 9) == 0)
        return 2;
    else if (strncmp(line, "PRINT", 5) == 0)
        return 2;
    else if (strncmp(line, "WRITE", 5) == 0)
    {
        char *quote = strchr(line, '"');
        if (!quote)
            return 3;
        char *end = strchr(quote + 1, '"');
        if (!end)
            return 3;
        size_t str_len = end - (quote + 1);
        if (str_len > 255)
            str_len = 255;
        return 3 + str_len;
    }
    else if (strncmp(line, "JMP", 3) == 0)
        return 5;
    else if (strncmp(line, "ALLOC", 5) == 0)
        return 5;
    else if (strcmp(line, "NEWLINE") == 0)
        return 1;
    else if (strncmp(line, "JZ", 2) == 0)
        return 6;
    else if (strncmp(line, "JNZ", 3) == 0)
        return 6;
    else if (strncmp(line, "INC", 3) == 0)
        return 2;
    else if (strncmp(line, "DEC", 3) == 0)
        return 2;
    else if (strncmp(line, "CMP", 3) == 0)
        return 3;
    else if (strncmp(line, "STORE", 5) == 0)
        return 6;
    else if (strncmp(line, "LOAD", 4) == 0)
        return 6;
    else if (strncmp(line, "GROW", 4) == 0)
        return 5;
    else if (strncmp(line, "RESIZE", 6) == 0)
        return 5;
    else if (strncmp(line, "FREE", 4) == 0)
        return 5;
    else if (strncmp(line, "FOPEN", 5) == 0)
    {
        const char *quote = strchr(line, '"');
        if (!quote)
            return 3;
        const char *end = strchr(quote + 1, '"');
        if (!end)
            return 3;
        size_t str_len = end - (quote + 1);
        if (str_len > 255)
            str_len = 255;
        return 3 + str_len;
    }
    else if (strncmp(line, "FCLOSE", 6) == 0)
        return 2;
    else if (strncmp(line, "FREAD", 5) == 0)
        return 3;
    else if (strncmp(line, "FWRITE", 6) == 0)
    {
        const char *p = line + 6;
        while (*p && isspace(*p))
            p++;
        if (toupper(p[0]) == 'R')
            return 3;
        else
            return 6;
    }
    else if (strncmp(line, "FSEEK", 5) == 0)
    {
        const char *p = line + 5;
        while (*p && isspace(*p))
            p++;
        if (toupper(p[0]) == 'R')
            return 3;
        else
            return 6;
    }
    else if (strncmp(line, "LOADSTR", 7) == 0)
        return 6;
    else if (strncmp(line, "PRINT_STR", 9) == 0)
        return 2; 
    else if (strcmp(line, "HALT") == 0)
        return 1;
    fprintf(stderr, "Unknown instruction for size calculation: %s\n", line);
    exit(1);
}
uint8_t parse_register(const char *r, int lineno)
{
    if (r[0] != 'R')
    {
        fprintf(stderr, "Invalid register on line %d\n", lineno);
        exit(1);
    }
    char *end;
    long v = strtol(r + 1, &end, 10);
    if (*end != '\0' || v < 0 || v >= REGISTERS)
    {
        fprintf(stderr, "Invalid register on line %d\n", lineno);
        exit(1);
    }
    return (uint8_t)v;
}
uint8_t parse_file(const char *r, int lineno)
{
    if (r[0] != 'F')
    {
        fprintf(stderr, "Invalid file descriptor on line %d\n", lineno);
        exit(1);
    }
    char *end;
    long v = strtol(r + 1, &end, 10);
    if (*end != '\0' || v < 0 || v >= FILE_DESCRIPTORS)
    {
        fprintf(stderr, "Invalid file descriptor on line %d\n", lineno);
        exit(1);
    }
    return (uint8_t)v;
}
char *trim(char *s)
{
    while (isspace(*s))
        s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && (isspace(*end) || *end == '\r' || *end == '\n'))
        *end-- = '\0';
    return s;
}