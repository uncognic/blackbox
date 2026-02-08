#ifndef TOOLS_H
#define TOOLS_H
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>


typedef enum {
    DT_STRING = 1,
    DT_BYTE,
    DT_WORD,
    DT_DWORD,
    DT_QWORD
} DataType;

typedef struct {
    char name[32];
    uint32_t offset;
    uint32_t size;
    DataType type;
} DataEntry;

typedef struct {
    char name[32];
    uint32_t addr;
} Label;

static uint32_t find_data_offset(const DataEntry *data_entries, size_t data_count, const char *name, FILE *in, FILE *out);

uint32_t find_label(const char *name, Label *labels, size_t count);
void write_u32(FILE *out, uint32_t val);
size_t instr_size(const char *line);
uint8_t parse_register(const char *r, int lineno);
char *trim(char *s);
uint8_t parse_file(const char *r, int lineno);
uint64_t get_true_random();

#endif