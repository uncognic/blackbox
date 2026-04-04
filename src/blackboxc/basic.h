#pragma once

#include <stddef.h>
#include <stdint.h>

int preprocess_basic(const char *input, const char *output, int debug);

typedef enum
{
    VAR_INT,
    VAR_STR
} VarType;

typedef struct
{
    char name[128];
    VarType type;
    uint8_t is_const;
    uint32_t slot;
    char data_name[128];
} Variable;

// the registers we claim
#define SCRATCH_MIN 0
#define SCRATCH_MAX 15
#define SCRATCH_COUNT 16

typedef struct
{
    uint32_t used;
} RegAlloc;

typedef struct
{
    Variable *vars;
    size_t count;
    size_t cap;
    uint32_t next_slot;
    uint32_t next_data_id;
} SymbolTable;

typedef struct
{
    char *data_sec; // for %data
    size_t data_len;
    size_t data_cap;

    char *code_sec;
    size_t code_len;
    size_t code_cap;
} OutBuf;

// nested IF/WHILE tracking

#define BLOCK_STACK_MAX 64

typedef enum
{
    BLOCK_IF,
    BLOCK_WHILE,
    BLOCK_FOR
} BlockKind;

typedef struct
{
    BlockKind kind;
    char end_label[64];
    char loop_label[64];
    int  has_else;
    char else_label[64];
    uint32_t for_var_slot;
    uint32_t for_limit_slot;
    uint32_t for_step_slot;
    char for_var_name[64];

} Block;

typedef struct
{
    Block items[BLOCK_STACK_MAX];
    int   top;
} BlockStack;