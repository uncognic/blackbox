//
// Created by User on 2026-04-22.
//

#ifndef BLACKBOX_BBX_CODEGEN_HPP
#define BLACKBOX_BBX_CODEGEN_HPP

#include "codegen.hpp"
#include <string>

namespace basic {
class BlackboxCodeGen final : public CodeGen {
  public:
    BlackboxCodeGen() = default;

    void emit_data_str(const std::string& name, const std::string& value) override;

    void emit_load_var(int reg, uint32_t slot, std::string_view comment = {}) override;
    void emit_store_var(int reg, uint32_t slot, std::string_view comment = {}) override;
    void emit_load_global(int reg, uint32_t slot, std::string_view comment = {}) override;
    void emit_store_global(int reg, uint32_t slot, std::string_view comment = {}) override;
    void emit_load_str(int reg, const std::string& data_name) override;
    void emit_load_ref(int dst, int src) override;
    void emit_store_ref(int dst, int src) override;

    void emit_movi(int reg, int32_t val) override;
    void emit_mov(int dst, int src) override;
    void emit_push(int reg) override;
    void emit_pop(int reg) override;

    void emit_add(int dst, int src) override;
    void emit_sub(int dst, int src) override;
    void emit_mul(int dst, int src) override;
    void emit_div(int dst, int src) override;
    void emit_mod(int dst, int src) override;
    void emit_inc(int reg) override;
    void emit_dec(int reg) override;

    void emit_and(int dst, int src) override;
    void emit_or(int dst, int src) override;
    void emit_xor(int dst, int src) override;
    void emit_not(int reg) override;
    void emit_shl(int dst, int src) override;
    void emit_shr(int dst, int src) override;

    void emit_cmp(int r1, int r2) override;
    void emit_jmp(const std::string& label) override;
    void emit_je(const std::string& label) override;
    void emit_jne(const std::string& label) override;
    void emit_jl(const std::string& label) override;
    void emit_jge(const std::string& label) override;
    void emit_jb(const std::string& label) override;
    void emit_jae(const std::string& label) override;
    void emit_call(const std::string& label) override;
    void emit_ret() override;
    void emit_halt(uint8_t code) override;

    void emit_label(const std::string& name) override;
    void emit_frame(uint32_t slots) override;
    void emit_globals(uint32_t count) override;
    void emit_entry_point() override;

    void emit_print_reg(int reg) override;
    void emit_print_str(int reg) override;
    void emit_print_char(int reg) override;
    void emit_eprint_reg(int reg) override;
    void emit_eprint_str(int reg) override;
    void emit_eprint_char(int reg) override;
    void emit_newline() override;
    void emit_enewline() override;
    void emit_read(int reg) override;
    void emit_readstr(int reg) override;
    void emit_readchar(int reg) override;

    void emit_fopen(const std::string& mode, uint8_t fd, const std::string& filename) override;
    void emit_fclose(uint8_t fd) override;
    void emit_fread(uint8_t fd, int reg) override;
    void emit_fwrite(uint8_t fd, int reg) override;
    void emit_fseek(uint8_t fd, int reg) override;

    void emit_sleep(int reg) override;
    void emit_exec(const std::string& cmd, int reg) override;
    void emit_rand(int reg) override;
    void emit_rand_range(int reg, const std::string& min_expr,
                         const std::string& max_expr) override;
    void emit_getkey(int reg) override;
    void emit_clrscr() override;
    void emit_getarg(int reg, uint32_t idx) override;
    void emit_getargc(int reg) override;
    void emit_getenv(int reg, const std::string& name) override;

    void emit_raw(std::string_view line) override;

    std::string get_data_section() const override;
    std::string get_code_section() const override;

  private:
    std::string data_sec_;
    std::string code_sec_;

    static std::string reg(int r);

    void code(std::string_view line);
    void code_comment(std::string_view comment, std::string_view line);
    void data(std::string_view line);
};
} // namespace basic
#endif // BLACKBOX_BBX_CODEGEN_HPP
