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
void write_u64(FILE *out, uint64_t val);
void write_i64(FILE *out, int64_t val);
size_t instr_size(const char *line);
uint8_t parse_register(const char *r, int lineno);
char *trim(char *s);
uint8_t parse_file(const char *r, int lineno);
uint64_t get_true_random();
int64_t read_i64(const uint8_t *data, size_t *pc);
uint64_t read_u64(const uint8_t *data, size_t *pc);

#endif