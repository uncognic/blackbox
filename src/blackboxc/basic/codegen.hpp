//
// Created by User on 2026-04-22.
//

#ifndef BLACKBOX_CODEGEN_HPP
#define BLACKBOX_CODEGEN_HPP
#include <cstdint>
#include <string>
#include <string_view>

namespace basic {
class CodeGen {
  public:
    virtual ~CodeGen() = default;

    virtual void emit_data_str(const std::string& name, const std::string& value) = 0;

    virtual void emit_load_var(int reg, uint32_t slot, std::string_view comment = {}) = 0;
    virtual void emit_store_var(int reg, uint32_t slot, std::string_view comment = {}) = 0;
    virtual void emit_load_global(int reg, const std::string& name,
                                  std::string_view comment = {}) = 0;
    virtual void emit_store_global(int reg, const std::string& name,
                                   std::string_view comment = {}) = 0;
    virtual void emit_load_str(int reg, const std::string& data_name) = 0;
    virtual void emit_load_ref(int dst, int src) = 0;
    virtual void emit_store_ref(int dst, int src) = 0;

    virtual void emit_movi(int reg, int32_t val) = 0;
    virtual void emit_mov(int dst, int src) = 0;
    virtual void emit_push(int reg) = 0;
    virtual void emit_pop(int reg) = 0;

    virtual void emit_add(int dst, int src) = 0;
    virtual void emit_sub(int dst, int src) = 0;
    virtual void emit_mul(int dst, int src) = 0;
    virtual void emit_div(int dst, int src) = 0;
    virtual void emit_mod(int dst, int src) = 0;
    virtual void emit_inc(int reg) = 0;
    virtual void emit_dec(int reg) = 0;
    virtual void emit_inc_var(uint32_t slot) = 0;
    virtual void emit_dec_var(uint32_t slot) = 0;
    virtual void emit_inc_global(const std::string& name) = 0;
    virtual void emit_dec_global(const std::string& name) = 0;

    virtual void emit_and(int dst, int src) = 0;
    virtual void emit_or(int dst, int src) = 0;
    virtual void emit_xor(int dst, int src) = 0;
    virtual void emit_not(int reg) = 0;
    virtual void emit_shl(int dst, int src) = 0;
    virtual void emit_shr(int dst, int src) = 0;

    virtual void emit_cmp(int r1, int r2) = 0;
    virtual void emit_jmp(const std::string& label) = 0;
    virtual void emit_je(const std::string& label) = 0;
    virtual void emit_jne(const std::string& label) = 0;
    virtual void emit_jl(const std::string& label) = 0;
    virtual void emit_jge(const std::string& label) = 0;
    virtual void emit_jb(const std::string& label) = 0;
    virtual void emit_jae(const std::string& label) = 0;
    virtual void emit_call(const std::string& label) = 0;
    virtual void emit_ret() = 0;
    virtual void emit_HLT(uint8_t code) = 0;

    virtual void emit_label(const std::string& name) = 0;
    virtual void emit_frame(uint32_t slots) = 0;
    virtual void emit_bss_symbol(const std::string& name) = 0;
    virtual void emit_entry_point() = 0;

    virtual void emit_print_reg(int reg) = 0;
    virtual void emit_print_str(int reg) = 0;
    virtual void emit_print_char(int reg) = 0;
    virtual void emit_eprint_reg(int reg) = 0;
    virtual void emit_eprint_str(int reg) = 0;
    virtual void emit_eprint_char(int reg) = 0;
    virtual void emit_newline() = 0;
    virtual void emit_enewline() = 0;
    virtual void emit_read(int reg) = 0;
    virtual void emit_readstr(int reg) = 0;
    virtual void emit_readchar(int reg) = 0;

    virtual void emit_fopen(const std::string& mode, uint8_t fd, const std::string& filename) = 0;
    virtual void emit_fclose(uint8_t fd) = 0;
    virtual void emit_fread(uint8_t fd, int reg) = 0;
    virtual void emit_fwrite(uint8_t fd, int reg) = 0;
    virtual void emit_fseek(uint8_t fd, int reg) = 0;

    virtual void emit_sleep(int reg) = 0;
    virtual void emit_exec(const std::string& cmd, int reg) = 0;
    virtual void emit_rand(int reg) = 0;
    virtual void emit_rand_range(int reg, const std::string& min_expr,
                                 const std::string& max_expr) = 0;
    virtual void emit_getkey(int reg) = 0;
    virtual void emit_clrscr() = 0;
    virtual void emit_getarg(int reg, uint32_t idx) = 0;
    virtual void emit_getargc(int reg) = 0;
    virtual void emit_getenv(int reg, const std::string& name) = 0;

    virtual void emit_raw(std::string_view line) = 0;

    virtual std::string get_data_section() const = 0;
    virtual std::string get_code_section() const = 0;

    virtual void emit_alloc(int size) = 0;
    virtual void emit_free(int size) = 0;
    virtual void emit_grow(int size) = 0;
    virtual void emit_resize(int size) = 0;

    virtual void emit_heap_read(int dst, int addr_reg) = 0;
virtual void emit_heap_write(int addr_reg, int src) = 0;
};
} // namespace basic
#endif // BLACKBOX_CODEGEN_HPP