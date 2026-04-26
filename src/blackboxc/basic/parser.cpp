//
// Created by User on 2026-04-22.
//
#include "parser.hpp"
#include "../../utils/string_utils.hpp"
#include "bbx_codegen.hpp"
#include <cctype>
#include <cstring>
#include <format>
#include <fstream>
#include <print>
#include <sstream>


namespace basic {
RegGuard::RegGuard(RegAlloc& ra) : ra(ra) {
    for (int i = 0; i < SCRATCH_COUNT; i++) {
        if (!(ra.used & (1u << i))) {
            ra.used |= (1u << i);
            reg = SCRATCH_MIN + i;
            return;
        }
    }
}

RegGuard::~RegGuard() {
    release();
}

void RegGuard::release() {
    if (reg >= SCRATCH_MIN && reg <= SCRATCH_MAX) {
        ra.used &= ~(1u << (reg - SCRATCH_MIN));
        reg = -1;
    }
}

Parser::Parser(CodeGen& codegen, bool debug)
    : cg_(codegen), debug_(debug), scope_(&global_slot_count_) {
}

std::string Parser::trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }
    size_t end = s.size();
    while (end > start && isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }
    return s.substr(start, end - start);
}

std::string Parser::trim(std::string_view s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return std::string(s);
}
bool Parser::starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); i++) {
        if (tolower(static_cast<unsigned char>(s[i])) !=
            tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    if (s.size() == prefix.size()) {
        return true;
    }
    if (!isalnum(static_cast<unsigned char>(prefix.back()))) {
        return true;
    }
    char next = s[prefix.size()];
    return !isalnum(static_cast<unsigned char>(next));
}

bool Parser::equals_ci(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (tolower(static_cast<unsigned char>(a[i])) !=
            tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

const char* Parser::skip_ws(const char* s) {
    while (*s && isspace(static_cast<unsigned char>(*s))) {
        s++;
    }
    return s;
}

std::string Parser::reg_name(int r) const {
    return std::format("R{:02}", r);
}

std::string Parser::make_label(std::string_view prefix) {
    return std::format("{}_{}", prefix, uid_++);
}

std::string Parser::error(std::string_view msg) const {
    return std::format("Error line {}: {}", lineno_, msg);
}

int Parser::ralloc_acquire() {
    for (int i = 0; i < SCRATCH_COUNT; i++) {
        if (!(ra_.used & (1u << i))) {
            ra_.used |= (1u << i);
            return SCRATCH_MIN + i;
        }
    }
    return -1;
}
void Parser::ralloc_release(int reg) {
    if (reg >= SCRATCH_MIN && reg <= SCRATCH_MAX) {
        ra_.used &= ~(1u << (reg - SCRATCH_MIN));
    }
}

Block* Parser::find_loop_block() {
    for (int i = static_cast<int>(block_stack_.size()) - 1; i >= 0; i--) {
        if (block_stack_[i].kind == BlockKind::While || block_stack_[i].kind == BlockKind::For) {
            return &block_stack_[i];
        }
    }
    return nullptr;
}

std::optional<uint8_t> Parser::get_file_handle_fd(const std::string& name) {
    for (auto& fh : file_handles_) {
        if (fh.name == name) {
            return fh.fd;
        }
    }
    return std::nullopt;
}

std::optional<uint8_t> Parser::alloc_file_handle_fd(const std::string& name) {
    if (auto fd = get_file_handle_fd(name)) {
        return fd;
    }
    if (next_file_handle_ == 0xFF) {
        return std::nullopt;
    }
    FileHandle fh;
    fh.name = name;
    fh.fd = next_file_handle_++;
    file_handles_.push_back(fh);
    return fh.fd;
}

CodeGen& Parser::active_cg() {
    if (current_func_) {
        return *current_func_->cg;
    }
    if (current_ns_) {
        return *current_ns_->cg;
    }
    return cg_;
}

Scope& Parser::active_scope() {
    if (current_func_) {
        return current_func_->scope;
    }
    if (current_ns_) {
        return current_ns_->scope;
    }
    return scope_;
}

const Parser::FuncEntry* Parser::find_func_entry(const std::string& full_name) const {
    for (auto& f : funcs_) {
        if (f.name == full_name) {
            return &f;
        }
    }
    for (auto& ns : namespaces_) {
        for (auto& f : ns.funcs) {
            if (f.name == full_name) {
                return &f;
            }
        }
    }
    return nullptr;
}

std::string Parser::resolve_func_name(const std::string& parsed_name) const {
    if (parsed_name.find("__") != std::string::npos) {
        return parsed_name;
    }
    if (current_ns_) {
        std::string ns_local = std::format("{}__{}", current_ns_->name, parsed_name);
        if (find_func_entry(ns_local)) {
            return ns_local;
        }
    }
    return parsed_name;
}

std::string Parser::get_additional_data_section() const {
    std::string out;
    for (auto& f : funcs_) {
        out += f.cg->get_data_section();
    }
    for (auto& ns : namespaces_) {
        out += ns.cg->get_data_section();
        for (auto& f : ns.funcs) {
            out += f.cg->get_data_section();
        }
    }
    return out;
}

std::string Parser::get_namespace_init_code_section() const {
    std::string out;
    for (auto& ns : namespaces_) {
        out += ns.cg->get_code_section();
    }
    return out;
}

std::string Parser::get_function_code_section() const {
    std::ostringstream out;

    auto emit_func = [&out](const FuncEntry& f) {
        out << "__bbx_func_" << f.name << ":\n";
        out << "    FRAME " << f.scope.next_local_slot() << "\n";
        out << f.cg->get_code_section();
        out << "    RET\n";
    };

    for (auto& f : funcs_) {
        emit_func(f);
    }
    for (auto& ns : namespaces_) {
        for (auto& f : ns.funcs) {
            emit_func(f);
        }
    }

    return out.str();
}

std::vector<std::string> Parser::get_global_names() const {
    std::vector<std::string> by_slot(global_slot_count_);

    auto collect = [&by_slot](const Scope& scope) {
        for (const auto& [slot, name] : scope.global_symbols()) {
            if (slot < by_slot.size() && by_slot[slot].empty()) {
                by_slot[slot] = name;
            }
        }
    };

    collect(scope_);

    for (const auto& f : funcs_) {
        collect(f.scope);
    }

    for (const auto& ns : namespaces_) {
        collect(ns.scope);
        for (const auto& f : ns.funcs) {
            collect(f.scope);
        }
    }

    std::vector<std::string> out;
    out.reserve(by_slot.size());
    for (const auto& name : by_slot) {
        if (!name.empty()) {
            out.push_back(name);
        }
    }

    return out;
}

std::optional<std::string> Parser::compile_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return error(std::format("Cannot open {}", path.string()));
    }

    std::string raw_line;
    while (std::getline(file, raw_line)) {
        lineno_++;

        std::string statement = raw_line;
        size_t comment = statement.find("//");
        if (comment != std::string::npos) {
            statement.erase(comment);
        }
        statement = trim(statement);
        if (statement.empty()) {
            continue;
        }

        if (starts_with_ci(statement, "NAMESPACE")) {
            if (current_func_ || current_ns_) {
                return error(std::format("Namespace cannot be nested", statement));
            }

            std::string ns_name = trim(statement.substr(10));
            if (ns_name.empty()) {
                return error("expected NAMESPACE <name>");
            }

            for (auto& ns : namespaces_) {
                if (ns.name == ns_name) {
                    return error(std::format("Namespace {} already defined", ns_name));
                }
            }

            namespaces_.emplace_back();
            current_ns_ = &namespaces_.back();
            current_ns_->name = ns_name;
            current_ns_->cg_owned = std::make_unique<BlackboxCodeGen>();
            current_ns_->cg = current_ns_->cg_owned.get();
            current_ns_->scope = Scope(&global_slot_count_, ns_name);
            current_ns_->scope.set_slot_start(scope_.next_local_slot());

            if (debug_) {
                std::println("[BASIC] NAMESPACE {}", ns_name);
            }

            continue;
        }

        if (equals_ci(statement, "ENDNAMESPACE")) {
            if (!current_ns_) {
                return error("ENDNAMESPACE without matching NAMESPACE");
            }

            if (!block_stack_.empty()) {
                return error(std::format("Unclosed block in namespace {}", current_ns_->name));
            }

            scope_.set_slot_start(current_ns_->scope.next_local_slot());
            if (debug_) {
                std::println("[BASIC] ENDNAMESPACE {}", current_ns_->name);
            }
            current_ns_ = nullptr;
            continue;
        }

        if (starts_with_ci(statement, "FUNC ")) {
            if (current_func_) {
                return error("nested FUNC is not allowed");
            }

            const char* p = statement.c_str() + 5;
            while (*p && isspace(static_cast<unsigned char>(*p))) {
                p++;
            }
            const char* colon = strchr(p, ':');
            if (!colon) {
                return error("expected FUNC <name>: ...");
            }

            std::string func_name = trim(std::string(p, colon - p));
            if (func_name.empty()) {
                return error("expected FUNC <name>: ...");
            }

            std::string full_name =
                current_ns_ ? (current_ns_->name + "__" + func_name) : func_name;

            auto& target = current_ns_ ? current_ns_->funcs : funcs_;
            for (auto& f : target) {
                if (f.name == full_name) {
                    return error(std::format("function '{}' already defined", func_name));
                }
            }

            target.emplace_back();
            current_func_ = &target.back();
            current_func_->name = full_name;
            current_func_->cg_owned = std::make_unique<BlackboxCodeGen>();
            current_func_->cg = current_func_->cg_owned.get();
            current_func_->scope = Scope(&global_slot_count_);
            current_func_->scope = Scope(&global_slot_count_);
            if (current_ns_) {
                current_func_->scope.set_parent(&current_ns_->scope);
            } else {
                current_func_->scope.set_parent(&scope_);
            }

            p = skip_ws(colon + 1);
            while (*p) {
                bool is_ref = false;
                bool is_str = false;

                if (*p == '&') {
                    is_ref = true;
                    p = skip_ws(p + 1);
                } else if (starts_with_ci(std::string_view(p), "VAR")) {
                    p = skip_ws(p + 3);
                } else if (starts_with_ci(std::string_view(p), "STR")) {
                    is_str = true;
                    p = skip_ws(p + 3);
                } else {
                    return error("expected VAR, STR, or & before param name");
                }

                std::string param_name;
                while (*p && (isalnum(static_cast<unsigned char>(*p)) || *p == '_')) {
                    param_name += *p++;
                }

                if (param_name.empty()) {
                    return error("expected parameter name");
                }

                current_func_->params.push_back(param_name);
                current_func_->param_is_ref.push_back(is_ref);

                if (is_ref) {
                    current_func_->scope.add_ref(param_name);
                } else if (is_str) {
                    current_func_->scope.add_str(param_name, std::format("_fparam_{}", param_name),
                                                 false);
                } else {
                    current_func_->scope.add_int(param_name);
                }

                p = skip_ws(p);
                if (*p == ',') {
                    p = skip_ws(p + 1);
                    continue;
                }
                if (*p == '\0') {
                    break;
                }
                return error("expected ',' between parameters");
            }

            for (int idx = static_cast<int>(current_func_->params.size()) - 1; idx >= 0; --idx) {
                Variable* param_var = current_func_->scope.find(current_func_->params[idx]);
                if (!param_var) {
                    return error(std::format("internal error: missing parameter '{}'",
                                             current_func_->params[idx]));
                }
                int r = ralloc_acquire();
                if (r < 0) {
                    return error("out of scratch registers");
                }
                active_cg().emit_pop(r);
                active_cg().emit_store_var(r, param_var->slot, param_var->name);
                ralloc_release(r);
            }

            if (debug_) {
                std::println("[BASIC] FUNC {} ({} params)", func_name,
                             current_func_->params.size());
            }
            continue;
        }

        if (equals_ci(statement, "ENDFUNC")) {
            if (!current_func_) {
                return error("ENDFUNC without FUNC");
            }
            if (!block_stack_.empty()) {
                return error(std::format("unclosed block in FUNC '{}'", current_func_->name));
            }
            if (debug_) {
                std::println("[BASIC] ENDFUNC {}", current_func_->name);
            }
            current_func_ = nullptr;
            continue;
        }

        if (equals_ci(statement, "ASM:")) {
            // read until ENDASM
            while (std::getline(file, raw_line)) {
                lineno_++;
                std::string asm_line = trim(raw_line);
                if (equals_ci(asm_line, "ENDASM")) {
                    break;
                }
                active_cg().emit_raw(asm_line);
            }
            continue;
        }

        if (auto err = compile_line(statement)) {
            return err;
        }
    }

    if (current_func_) {
        return error(std::format("unterminated FUNC '{}'", current_func_->name));
    }
    if (current_ns_) {
        return error(std::format("unterminated NAMESPACE '{}'", current_ns_->name));
    }
    if (!block_stack_.empty()) {
        return error("unclosed block at end of file");
    }

    return std::nullopt;
}

std::optional<std::string> Parser::compile_line(const std::string& s) {
    if (starts_with_ci(s, "@ENTRY")) {
        if (current_func_) {
            return error("@ENTRY not allowed inside FUNC");
        }
        if (entry_point_) {
            return error("multiple @ENTRY directives");
        }
        active_cg().emit_entry_point();
        entry_point_ = true;
        if (debug_) {
            std::println("[BASIC] @ENTRY");
        }
        return std::nullopt;
    }

    if (starts_with_ci(s, "GLOBAL VAR ")) {
        return stmt_var(s.substr(11), true);
    }
    if (starts_with_ci(s, "GLOBAL CONST ")) {
        return stmt_const(s.substr(13)); // no need here because CONSTs are "gobal"
    }
    if (starts_with_ci(s, "CONST ")) {
        return stmt_const(s);
    }
    if (starts_with_ci(s, "VAR ")) {
        return stmt_var(s, false);
    }

    if (starts_with_ci(s, "IF ")) {
        return stmt_if(s);
    }
    if (starts_with_ci(s, "ELSE IF ") || starts_with_ci(s, "ELSE IF:")) {
        return stmt_else_if(s);
    }
    if (equals_ci(s, "ELSE:")) {
        return stmt_else();
    }
    if (equals_ci(s, "ENDIF")) {
        return stmt_endif();
    }

    if (starts_with_ci(s, "WHILE ")) {
        return stmt_while(s);
    }
    if (equals_ci(s, "ENDWHILE")) {
        return stmt_endwhile();
    }

    if (starts_with_ci(s, "FOR ")) {
        return stmt_for(s);
    }
    if (starts_with_ci(s, "NEXT")) {
        return stmt_next(s);
    }

    if (equals_ci(s, "BREAK")) {
        return stmt_break();
    }
    if (equals_ci(s, "CONTINUE")) {
        return stmt_continue();
    }

    if (starts_with_ci(s, "EPRINT ") || equals_ci(s, "EPRINT")) {
        return stmt_print(s, true);
    }
    if (starts_with_ci(s, "PRINT ") || equals_ci(s, "PRINT")) {
        return stmt_print(s, false);
    }
    if (starts_with_ci(s, "EWRITE")) {
        return stmt_write(s, true);
    }
    if (starts_with_ci(s, "WRITE")) {
        return stmt_write(s, false);
    }

    if (starts_with_ci(s, "INPUT ")) {
        return stmt_input(s);
    }
    if (starts_with_ci(s, "CALL ")) {
        return stmt_call(s);
    }
    if (starts_with_ci(s, "RETURN")) {
        return stmt_return(s);
    }
    if (starts_with_ci(s, "HALT")) {
        return stmt_halt(s);
    }
    if (starts_with_ci(s, "GOTO ")) {
        return stmt_goto(s);
    }
    if (starts_with_ci(s, "LABEL ")) {
        return stmt_label(s);
    }
    if (starts_with_ci(s, "SLEEP")) {
        return stmt_sleep(s);
    }
    if (starts_with_ci(s, "RANDOM")) {
        return stmt_random(s);
    }
    if (starts_with_ci(s, "INC ") || starts_with_ci(s, "INC\t")) {
        return stmt_inc(s);
    }
    if (starts_with_ci(s, "DEC ") || starts_with_ci(s, "DEC\t")) {
        return stmt_dec(s);
    }
    if (starts_with_ci(s, "EXEC")) {
        return stmt_exec(s);
    }

    if (starts_with_ci(s, "FOPEN")) {
        return stmt_fopen(s);
    }
    if (starts_with_ci(s, "FCLOSE")) {
        return stmt_fclose(s);
    }
    if (starts_with_ci(s, "FREAD")) {
        return stmt_fread(s);
    }
    if (starts_with_ci(s, "FWRITE")) {
        return stmt_fwrite(s);
    }
    if (starts_with_ci(s, "FSEEK")) {
        return stmt_fseek(s);
    }
    if (starts_with_ci(s, "FPRINT")) {
        return stmt_fprint(s);
    }

    if (starts_with_ci(s, "GETARGC")) {
        return stmt_getargc(s);
    }
    if (starts_with_ci(s, "GETARG")) {
        return stmt_getarg(s);
    }
    if (starts_with_ci(s, "GETENV")) {
        return stmt_getenv(s);
    }
    if (starts_with_ci(s, "GETKEY")) {
        return stmt_getkey(s);
    }
    if (equals_ci(s, "CLRSCR")) {
        return stmt_clrscr();
    }
    if (starts_with_ci(s, "FOREACH")) {
        return stmt_foreach(s);
    }


    if (auto err = stmt_assign(s); err != std::optional<std::string>("__no_match__")) {
        return err;
    }

    return error(std::format("unknown statement: '{}'", s));
}
} // namespace basic