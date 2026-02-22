#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>

typedef enum
{
    DATA_STRING,
    DATA_DWORD,
    DATA_QWORD,
    DATA_WORD,
    DATA_BYTE
} DataType;

typedef struct
{
    char name[32];
    DataType type;
    char *str;
    uint8_t byte;
    uint16_t word;
    uint32_t dword;
    uint64_t qword;
    uint32_t offset;
} Data;

uint32_t find_data(const char *name, Data *data, size_t count);

typedef struct
{
    char name[32];
    uint32_t addr;
    uint32_t frame_size;
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

typedef struct
{
    char *name;
    char **params;
    int paramc;
    char **body;
    int bodyc;
} Macro;

Macro *find_macro(Macro *macros, size_t macro_count, const char *name);
char *replace_all(const char *src, const char *find, const char *repl);
int expand_invocation(const char *invocation_line, FILE *dest, int depth, Macro *macros, size_t macro_count, unsigned long *expand_id);
