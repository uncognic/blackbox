//
// Created by User on 2026-04-22.
//

#include "bbx_codegen.hpp"
#include <format>

namespace basic {

std::string BlackboxCodeGen::reg(int r) {
    return std::format("R{:02}", r);
}

void BlackboxCodeGen::code(std::string_view line) {
    code_sec_ += line;
    code_sec_ += '\n';
}

void BlackboxCodeGen::code_comment(std::string_view comment, std::string_view line) {
    if (!comment.empty()) {
        code_sec_ += std::format("    ; {}\n", comment);
    }
    code_sec_ += line;
    code_sec_ += '\n';
}

void BlackboxCodeGen::data(std::string_view line) {
    data_sec_ += line;
    data_sec_ += '\n';
}

std::string BlackboxCodeGen::get_data_section() const {
    return data_sec_;
}
std::string BlackboxCodeGen::get_code_section() const {
    return code_sec_;
}

void BlackboxCodeGen::emit_data_str(const std::string& name, const std::string& value) {
    data(std::format("    {} \"{}\"", name, value));
}

// vars
void BlackboxCodeGen::emit_load_var(int r, uint32_t slot, std::string_view comment) {
    code_comment(comment, std::format("    MOV {}, VAR {}", reg(r), slot));
}

void BlackboxCodeGen::emit_store_var(int r, uint32_t slot, std::string_view comment) {
    code_comment(comment, std::format("    MOV VAR {}, {}", slot, reg(r)));
}
void BlackboxCodeGen::emit_load_global(int r, const std::string& name, std::string_view comment) {
    code_comment(comment, std::format("    MOV {}, [{}]", reg(r), name));
}
void BlackboxCodeGen::emit_store_global(int r, const std::string& name, std::string_view comment) {
    code_comment(comment, std::format("    MOV [{}], {}", name, reg(r)));
}

void BlackboxCodeGen::emit_bss_symbol(const std::string& name) {
    data(std::format("    {}", name));
}
void BlackboxCodeGen::emit_load_str(int r, const std::string& data_name) {
    code(std::format("    LOADSTR ${}, {}", data_name, reg(r)));
}

void BlackboxCodeGen::emit_load_ref(int dst, int src) {
    code(std::format("    LOADREF {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_store_ref(int dst, int src) {
    code(std::format("    STOREREF {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_movi(int r, int32_t val) {
    code(std::format("    MOV {}, {}", reg(r), val));
}
void BlackboxCodeGen::emit_mov(int dst, int src) {
    code(std::format("    MOV {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_push(int r) {
    code(std::format("    PUSH {}", reg(r)));
}

void BlackboxCodeGen::emit_pop(int r) {
    code(std::format("    POP {}", reg(r)));
}

void BlackboxCodeGen::emit_add(int dst, int src) {
    code(std::format("    ADD {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_sub(int dst, int src) {
    code(std::format("    SUB {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_mul(int dst, int src) {
    code(std::format("    MUL {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_div(int dst, int src) {
    code(std::format("    DIV {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_mod(int dst, int src) {
    code(std::format("    MOD {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_inc(int r) {
    code(std::format("    INC {}", reg(r)));
}

void BlackboxCodeGen::emit_dec(int r) {
    code(std::format("    DEC {}", reg(r)));
}

void BlackboxCodeGen::emit_and(int dst, int src) {
    code(std::format("    AND {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_or(int dst, int src) {
    code(std::format("    OR {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_xor(int dst, int src) {
    code(std::format("    XOR {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_not(int r) {
    code(std::format("    NOT {}", reg(r)));
}

void BlackboxCodeGen::emit_shl(int dst, int src) {
    code(std::format("    SHL {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_shr(int dst, int src) {
    code(std::format("    SHR {}, {}", reg(dst), reg(src)));
}

void BlackboxCodeGen::emit_cmp(int r1, int r2) {
    code(std::format("    CMP {}, {}", reg(r1), reg(r2)));
}

void BlackboxCodeGen::emit_jmp(const std::string& label) {
    code(std::format("    JMP {}", label));
}

void BlackboxCodeGen::emit_je(const std::string& label) {
    code(std::format("    JE {}", label));
}

void BlackboxCodeGen::emit_jne(const std::string& label) {
    code(std::format("    JNE {}", label));
}

void BlackboxCodeGen::emit_jl(const std::string& label) {
    code(std::format("    JL {}", label));
}

void BlackboxCodeGen::emit_jge(const std::string& label) {
    code(std::format("    JGE {}", label));
}

void BlackboxCodeGen::emit_jb(const std::string& label) {
    code(std::format("    JB {}", label));
}

void BlackboxCodeGen::emit_jae(const std::string& label) {
    code(std::format("    JAE {}", label));
}

void BlackboxCodeGen::emit_call(const std::string& label) {
    code(std::format("    CALL {}", label));
}

void BlackboxCodeGen::emit_ret() {
    code("    RET");
}

void BlackboxCodeGen::emit_halt(uint8_t code_val) {
    if (code_val == 0) {
        code("    HALT OK");
    } else if (code_val == 1) {
        code("    HALT BAD");
    } else {
        code(std::format("    HALT {}", code_val));
    }
}

void BlackboxCodeGen::emit_label(const std::string& name) {
    code(std::format(".{}:", name));
}

void BlackboxCodeGen::emit_frame(uint32_t slots) {
    code(std::format("    FRAME {}", slots));
}

void BlackboxCodeGen::emit_entry_point() {
    code(".__bbx_basic_main:");
}
void BlackboxCodeGen::emit_print_reg(int r) {
    code(std::format("    PRINTREG {}", reg(r)));
}

void BlackboxCodeGen::emit_print_str(int r) {
    code(std::format("    PRINTSTR {}", reg(r)));
}

void BlackboxCodeGen::emit_print_char(int r) {
    code(std::format("    PRINTCHAR {}", reg(r)));
}

void BlackboxCodeGen::emit_eprint_reg(int r) {
    code(std::format("    EPRINTREG {}", reg(r)));
}

void BlackboxCodeGen::emit_eprint_str(int r) {
    code(std::format("    EPRINTSTR {}", reg(r)));
}

void BlackboxCodeGen::emit_eprint_char(int r) {
    code(std::format("    EPRINTCHAR {}", reg(r)));
}

void BlackboxCodeGen::emit_newline() {
    code("    MOV R01, 10");
    code("    PRINTCHAR R01");
}

void BlackboxCodeGen::emit_enewline() {
    code("    MOV R01, 10");
    code("    EPRINTCHAR R01");
}

void BlackboxCodeGen::emit_read(int r) {
    code(std::format("    READ {}", reg(r)));
}

void BlackboxCodeGen::emit_readstr(int r) {
    code(std::format("    READSTR {}", reg(r)));
}

void BlackboxCodeGen::emit_readchar(int r) {
    code(std::format("    READCHAR {}", reg(r)));
}

void BlackboxCodeGen::emit_fopen(const std::string& mode, uint8_t fd, const std::string& filename) {
    code(std::format("    FOPEN {}, F{}, \"{}\"", mode, fd, filename));
}

void BlackboxCodeGen::emit_fclose(uint8_t fd) {
    code(std::format("    FCLOSE F{}", fd));
}

void BlackboxCodeGen::emit_fread(uint8_t fd, int r) {
    code(std::format("    FREAD F{}, {}", fd, reg(r)));
}

void BlackboxCodeGen::emit_fwrite(uint8_t fd, int r) {
    code(std::format("    FWRITE F{}, {}", fd, reg(r)));
}

void BlackboxCodeGen::emit_fseek(uint8_t fd, int r) {
    code(std::format("    FSEEK F{}, {}", fd, reg(r)));
}

void BlackboxCodeGen::emit_sleep(int r) {
    code(std::format("    SLEEP {}", reg(r)));
}

void BlackboxCodeGen::emit_exec(const std::string& cmd, int r) {
    code(std::format("    EXEC \"{}\", {}", cmd, reg(r)));
}

void BlackboxCodeGen::emit_rand(int r) {
    code(std::format("    RAND {}", reg(r)));
}

void BlackboxCodeGen::emit_rand_range(int r, const std::string& min_expr,
                                      const std::string& max_expr) {
    code(std::format("    RAND {}, {}, {}", reg(r), min_expr, max_expr));
}

void BlackboxCodeGen::emit_getkey(int r) {
    code(std::format("    GETKEY {}", reg(r)));
}

void BlackboxCodeGen::emit_clrscr() {
    code("    CLRSCR");
}

void BlackboxCodeGen::emit_getarg(int r, uint32_t idx) {
    code(std::format("    GETARG {}, {}", reg(r), idx));
}

void BlackboxCodeGen::emit_getargc(int r) {
    code(std::format("    GETARGC {}", reg(r)));
}

void BlackboxCodeGen::emit_getenv(int r, const std::string& name) {
    code(std::format("    GETENV {}, \"{}\"", reg(r), name));
}

void BlackboxCodeGen::emit_raw(std::string_view line) {
    code(line);
}
} // namespace basic