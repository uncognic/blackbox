#pragma once

#include <cstddef>
#include <cstdint>

#include <string>
#include <vector>

int preprocess_basic(const char* input, const char* output, int debug);

typedef enum { VAR_INT, VAR_STR } VarType;

typedef struct {
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

typedef struct {
    uint32_t used;
} RegAlloc;

typedef struct {
    std::vector<Variable> vars;
    uint32_t next_slot;
    uint32_t next_data_id;
} SymbolTable;

typedef struct {
    std::string data_sec; // for %data
    std::string code_sec;
} OutBuf;

// nested IF/WHILE tracking

typedef enum { BLOCK_IF, BLOCK_WHILE, BLOCK_FOR } BlockKind;

typedef struct {
    BlockKind kind;
    char end_label[64];
    char loop_label[64];
    int has_else;
    char else_label[64];
    uint32_t for_var_slot;
    uint32_t for_limit_slot;
    uint32_t for_step_slot;
    char for_var_name[64];
} Block;

typedef struct {
    std::vector<Block> items;
} BlockStack;

typedef struct {
    std::string name;
    uint8_t fd;
} FileHandle;

class CompilerState {
  public:
    SymbolTable st;
    OutBuf ob;
    RegAlloc ra;
    BlockStack bs;
    unsigned long uid = 0;
    std::vector<FileHandle> file_handles;
    uint8_t next_file_handle = 0;
    int lineno = 0;
    int debug = 0;
    char emit_ctx[512] = {};

    CompilerState();

    Variable* sym_find(const char* name);
    Variable* sym_add_int(const char* name);
    Variable* sym_add_str(const char* name, const char* data_name, int is_const);

    int ralloc_acquire();
    void ralloc_release(int reg);

    void set_emit_context(const char* stmt);
    int emit_data(const char* fmt, ...);
    int emit_code_comment(const char* detail, const char* fmt, ...);

    int get_file_handle_fd(const char* name, uint8_t* out_fd);
    int alloc_file_handle_fd(const char* name, uint8_t* out_fd);

    void bstack_push(Block b);
    Block* bstack_peek();
    Block bstack_pop();

    int emit_atom(const char* s, const char** end, int* out_reg);
    int emit_unary(const char* s, const char** end, int* out_reg);
    int emit_bitwise(const char* s, const char** end, int* out_reg);
    int emit_mul(const char* s, const char** end, int* out_reg);
    int emit_expr(const char* s, int* out_reg);
    int emit_expr_p(const char* s, const char** end, int* out_reg);
    int emit_condition(const char* s, const char* skip_label);
    int emit_write_values(const char* arg, const char* stmt_name, int to_stderr);

    int compile_line(char* s);
};

struct FuncDef {
    std::string name;
    std::vector<std::string> params;
    CompilerState state;
};