#ifndef TOOLS_H
#define TOOLS_H
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>

typedef struct {
    char name[32];
    uint32_t offset;
} String;

uint32_t find_string(const char *name, String *strings, size_t count);

typedef struct {
    char name[32];
    uint32_t addr;
} Label;

uint32_t find_label(const char *name, Label *labels, size_t count);
void write_u32(FILE *out, uint32_t val);
size_t instr_size(const char *line);
uint8_t parse_register(const char *r, int lineno);
char *trim(char *s);
uint8_t parse_file(const char *r, int lineno);


#endif