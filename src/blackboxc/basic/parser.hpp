//
// Created by User on 2026-04-22.
//

#ifndef BLACKBOX_PARSER_HPP
#define BLACKBOX_PARSER_HPP
#include "codegen.hpp"
#include "scope.hpp"
#include "types.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace basic {
struct RegGuard {
    RegAlloc& ra;
    int reg = -1;

    explicit RegGuard(RegAlloc& ra);
    ~RegGuard();

    void release();
    bool ok() const { return reg >= 0; }
    operator int() const { return reg; }

    RegGuard(const RegGuard&) = delete;
    RegGuard& operator=(const RegGuard&) = delete;

    
};

class Parser {
  public:
    Parser(CodeGen& codegen, bool debug);

    std::optional<std::string> compile_file(const std::filesystem::path& path);

    uint32_t get_local_slot_count() const { return scope_.next_local_slot(); }
    uint32_t get_global_slot_count() const { return global_slot_count_; }
    bool has_entry_point() const { return entry_point_; }
    std::string get_additional_data_section() const;
    std::string get_namespace_init_code_section() const;
    std::string get_function_code_section() const;
    std::vector<std::string> get_global_names() const;

  private:
    CodeGen& cg_;
    bool debug_;
    int lineno_ = 0;

    RegAlloc ra_;
    unsigned long uid_ = 0;

    uint32_t global_slot_count_ = 0;

    Scope scope_;

    std::vector<Block> block_stack_;

    std::string current_namespace_;
    bool entry_point_ = false;

    std::vector<FileHandle> file_handles_;
    uint8_t next_file_handle_ = 0;

    struct FuncEntry {
        std::string name;
        std::vector<std::string> params;
        std::vector<bool> param_is_ref;
        Scope scope;
        CodeGen* cg = nullptr; // owns a BlackboxCodeGen
        std::unique_ptr<CodeGen> cg_owned;
    };

    struct NsEntry {
        std::string name;
        Scope scope;
        CodeGen* cg = nullptr;
        std::unique_ptr<CodeGen> cg_owned;
        std::vector<FuncEntry> funcs;
    };

    std::vector<FuncEntry> funcs_;
    std::vector<NsEntry> namespaces_;

    FuncEntry* current_func_ = nullptr;
    NsEntry* current_ns_ = nullptr;

    CodeGen& active_cg();
    Scope& active_scope();

    const FuncEntry* find_func_entry(const std::string& full_name) const;
    std::string resolve_func_name(const std::string& parsed_name) const;

    std::optional<std::string> compile_line(const std::string& s);

    std::optional<std::string> stmt_var(const std::string& s, bool is_global);
    std::optional<std::string> stmt_const(const std::string& s);
    std::optional<std::string> stmt_assign(const std::string& s);
    std::optional<std::string> stmt_if(const std::string& s);
    std::optional<std::string> stmt_else_if(const std::string& s);
    std::optional<std::string> stmt_else();
    std::optional<std::string> stmt_endif();
    std::optional<std::string> stmt_while(const std::string& s);
    std::optional<std::string> stmt_endwhile();
    std::optional<std::string> stmt_for(const std::string& s);
    std::optional<std::string> stmt_next(const std::string& s);
    std::optional<std::string> stmt_print(const std::string& s, bool to_stderr);
    std::optional<std::string> stmt_write(const std::string& s, bool to_stderr);
    std::optional<std::string> stmt_call(const std::string& s);
    std::optional<std::string> stmt_return(const std::string& s);
    std::optional<std::string> stmt_halt(const std::string& s);
    std::optional<std::string> stmt_input(const std::string& s);
    std::optional<std::string> stmt_break();
    std::optional<std::string> stmt_continue();
    std::optional<std::string> stmt_goto(const std::string& s);
    std::optional<std::string> stmt_label(const std::string& s);
    std::optional<std::string> stmt_sleep(const std::string& s);
    std::optional<std::string> stmt_random(const std::string& s);
    std::optional<std::string> stmt_inc(const std::string& s);
    std::optional<std::string> stmt_dec(const std::string& s);
    std::optional<std::string> stmt_exec(const std::string& s);
    std::optional<std::string> stmt_fopen(const std::string& s);
    std::optional<std::string> stmt_fclose(const std::string& s);
    std::optional<std::string> stmt_fread(const std::string& s);
    std::optional<std::string> stmt_fwrite(const std::string& s);
    std::optional<std::string> stmt_fseek(const std::string& s);
    std::optional<std::string> stmt_fprint(const std::string& s);
    std::optional<std::string> stmt_getarg(const std::string& s);
    std::optional<std::string> stmt_getargc(const std::string& s);
    std::optional<std::string> stmt_getenv(const std::string& s);
    std::optional<std::string> stmt_getkey(const std::string& s);
    std::optional<std::string> stmt_clrscr();

    std::optional<std::string> emit_atom(const char* s, const char** end, int* out_reg);
    std::optional<std::string> emit_unary(const char* s, const char** end, int* out_reg);
    std::optional<std::string> emit_bitwise(const char* s, const char** end, int* out_reg);
    std::optional<std::string> emit_mul_expr(const char* s, const char** end, int* out_reg);
    std::optional<std::string> emit_expr(const char* s, int* out_reg);
    std::optional<std::string> emit_expr_p(const char* s, const char** end, int* out_reg);
    std::optional<std::string> emit_condition(const char* s, const std::string& skip_label);
    std::optional<std::string> emit_write_values(const char* arg, std::string_view stmt_name,
                                                 bool to_stderr);

    int ralloc_acquire();
    void ralloc_release(int reg);
    std::string reg_name(int r) const;
    std::string make_label(std::string_view prefix);
    std::string error(std::string_view msg) const;

    std::optional<uint8_t> get_file_handle_fd(const std::string& name);
    std::optional<uint8_t> alloc_file_handle_fd(const std::string& name);

    Block* find_loop_block();

    static std::string trim(const std::string& s);
    static std::string trim(std::string_view s);
    static bool starts_with_ci(std::string_view s, std::string_view prefix);
    static bool equals_ci(std::string_view a, std::string_view b);
    static const char* skip_ws(const char* s);
};
} // namespace basic
#endif // BLACKBOX_PARSER_HPP
