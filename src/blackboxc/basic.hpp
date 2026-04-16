#pragma once

#include <cstdint>

#include <string>
#include <vector>

int preprocess_basic(const char* input, const char* output, int debug);

enum VarType { VAR_INT, VAR_STR };

struct Variable {
    char name[128];
    VarType type;
    uint8_t is_const;
    uint32_t slot;
    char data_name[128];
    bool is_ref;
};

// the registers we claim
constexpr int SCRATCH_MIN = 0;
constexpr int SCRATCH_MAX = 15;
constexpr int SCRATCH_COUNT = 16;

struct RegAlloc {
    uint32_t used;
};

struct SymbolTable {
    std::vector<Variable> vars;
    uint32_t next_slot = 0;
    uint32_t next_data_id = 0;
};

struct OutBuf {
    std::string data_sec; // for %data
    std::string code_sec;
};

// nested IF/WHILE tracking

enum BlockKind { BLOCK_IF, BLOCK_WHILE, BLOCK_FOR };

struct Block {
    BlockKind kind;
    char end_label[64];
    char loop_label[64];
    int has_else;
    char else_label[64];
    uint32_t for_var_slot;
    uint32_t for_limit_slot;
    uint32_t for_step_slot;
    char for_var_name[64];
};

struct BlockStack {
    std::vector<Block> items;
};

struct FileHandle {
    std::string name;
    uint8_t fd = 0;
};

struct FuncDef;

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
    const std::vector<FuncDef>* funcs = nullptr;
    bool in_func = false;
    bool entry_point_declared = false;

    std::string current_namespace;

    CompilerState();

    Variable* sym_find(const char* name);
    Variable* sym_add_int(const char* name);
    Variable* sym_add_str(const char* name, const char* data_name, int is_const);
    Variable* sym_add_ref(const char* name);

    int ralloc_acquire();
    void ralloc_release(int reg);

    void set_emit_context(const char* stmt);
    int emit_data(const char* fmt, ...);
    int emit_code_comment(const char* detail, const char* fmt, ...);

    int get_file_handle_fd(const char* name, uint8_t* out_fd);
    int alloc_file_handle_fd(const char* name, uint8_t* out_fd);

    void bstack_push(const Block& b);
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
    std::vector<bool> param_is_ref;
    CompilerState state;
};

struct NamespaceDef {
    std::string name;
    std::vector<FuncDef> funcs;
    CompilerState state;
};