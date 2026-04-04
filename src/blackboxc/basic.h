#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

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
    std::vector<Variable> vars;
    uint32_t next_slot;
    uint32_t next_data_id;
} SymbolTable;

typedef struct
{
    std::string data_sec; // for %data
    std::string code_sec;
} OutBuf;

// nested IF/WHILE tracking

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
    std::vector<Block> items;
} BlockStack;
