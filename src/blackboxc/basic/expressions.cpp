//
// Created by User on 2026-04-22.
//
#include "parser.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>

namespace basic {
std::optional<std::string> Parser::emit_atom(const char* s, const char** end, int* out_reg) {
    s = skip_ws(s);

    // parenthesized expression
    if (*s == '(') {
        s++;
        if (auto err = emit_expr(s, out_reg)) {
            return err;
        }
        s = skip_ws(*end);
        if (*s != ')') {
            return error("expected ')' in expression");
        }
        *end = s + 1;
        return std::nullopt;
    }

    // numeric literal
    if (*s == '-' || isdigit(static_cast<unsigned char>(*s))) {
        char* endptr;
        auto val = static_cast<int32_t>(strtol(s, &endptr, 10));
        int r = ralloc_acquire();
        if (r < 0) {
            return error("out of scratch registers");
        }
        active_cg().emit_movi(r, val);
        *out_reg = r;
        *end = endptr;
        return std::nullopt;
    }

    if (isalpha(static_cast<unsigned char>(*s)) || *s == '_') {
        std::string name;
        const char* p = s;
        while (*p && (isalnum(static_cast<unsigned char>(*p)) || *p == '_')) {
            name += *p++;
        }

        // namespace call
        while (*skip_ws(p) == '.') {
            p = skip_ws(p) + 1;
            name += "__";
            while (*p && (isalnum(static_cast<unsigned char>(*p)) || *p == '_')) {
                name += *p++;
            }
        }

        const char* after = skip_ws(p);

        if (*after == '[') {
            auto iterator = arrays_.find(name);
            if (iterator != arrays_.end()) {
                const ArrayInfo& array_info = iterator->second;
                const char* index_ptr = skip_ws(after + 1);
                int index_reg;
                if (auto err = emit_expr_p(index_ptr, &index_ptr, &index_reg)) {
                    return err;
                }
                index_ptr = skip_ws(index_ptr);
                if (*index_ptr != ']') {
                    return error("expected ']' in array index");
                }
                *end = index_ptr + 1;

                int addr_reg = ralloc_acquire();
                active_cg().emit_movi(addr_reg, static_cast<int32_t>(array_info.base));
                active_cg().emit_add(addr_reg, index_reg);
                ralloc_release(index_reg);

                int dst = ralloc_acquire();
                active_cg().emit_heap_read(dst, addr_reg);
                ralloc_release(addr_reg);
                *out_reg = dst;
                return std::nullopt;
            }
        }

        // function call
        if (*after == '(') {
            std::string resolved_name = resolve_func_name(name);
            const FuncEntry* fd = find_func_entry(resolved_name);
            const char* argp = skip_ws(after + 1);
            int arg_index = 0;

            while (*argp && *argp != ')') {
                bool is_ref = fd && arg_index < static_cast<int>(fd->param_is_ref.size()) &&
                              fd->param_is_ref[arg_index];

                if (is_ref) {
                    std::string refname;
                    while (*argp && (isalnum(static_cast<unsigned char>(*argp)) || *argp == '_')) {
                        refname += *argp++;
                    }
                    Variable* rv = active_scope().find(refname);
                    if (!rv) {
                        return error(std::format("undefined variable '{}'", refname));
                    }
                    int r = ralloc_acquire();
                    if (r < 0) {
                        return error("out of scratch registers");
                    }
                    active_cg().emit_movi(r, static_cast<int32_t>(rv->slot));
                    active_cg().emit_push(r);
                    ralloc_release(r);
                } else {
                    argp = skip_ws(argp);
                    if (*argp == '"') {
                        const char* str_end = strchr(argp + 1, '"');
                        if (!str_end) {
                            return error("unterminated string in call");
                        }
                        std::string dname = std::format("_p{}", uid_++);
                        active_cg().emit_data_str(dname, std::string(argp + 1, str_end - argp - 1));
                        int r = ralloc_acquire();
                        if (r < 0) {
                            return error("out of scratch registers");
                        }
                        active_cg().emit_load_str(r, dname);
                        active_cg().emit_push(r);
                        ralloc_release(r);
                        argp = str_end + 1;
                    } else {
                        int arg_reg;
                        if (auto err = emit_expr_p(argp, &argp, &arg_reg)) {
                            return err;
                        }
                        active_cg().emit_push(arg_reg);
                        ralloc_release(arg_reg);
                    }
                }

                arg_index++;
                argp = skip_ws(argp);
                if (*argp == ',') {
                    argp = skip_ws(argp + 1);
                    continue;
                }
                if (*argp == ')') {
                    break;
                }
                return error(std::format("expected ',' or ')' in call to '{}'", name));
            }

            if (*argp != ')') {
                return error(std::format("expected ')' in call to '{}'", name));
            }
            *end = argp + 1;

            active_cg().emit_call(std::format("__bbx_func_{}", resolved_name));
            int r = ralloc_acquire();
            if (r < 0) {
                return error("out of scratch registers");
            }
            active_cg().emit_mov(r, 0); // R00 is standard return register
            *out_reg = r;
            return std::nullopt;
        }

        // plain variable
        *end = p;
        Variable* v = active_scope().find(name);
        if (!v && !current_ns_ && name.find("__") != std::string::npos) {
            for (auto& ns : namespaces_) {
                if (auto* nv = ns.scope.find(name)) {
                    v = nv;
                    break;
                }
            }
        }
        if (!v) {
            return error(std::format("undefined variable '{}'", name));
        }

        if (v->is_ref) {
            int slot_r = ralloc_acquire();
            int val_r = ralloc_acquire();
            if (slot_r < 0 || val_r < 0) {
                return error("out of scratch registers");
            }
            active_cg().emit_load_var(slot_r, v->slot, v->name);
            active_cg().emit_load_ref(val_r, slot_r);
            ralloc_release(slot_r);
            *out_reg = val_r;
        } else {
            int r = ralloc_acquire();
            if (r < 0) {
                return error("out of scratch registers");
            }
            if (v->is_global) {
                active_cg().emit_load_global(r, v->name, v->name);
            } else if (v->type == VarType::Int || !v->is_const) {
                active_cg().emit_load_var(r, v->slot, v->name);
            } else {
                active_cg().emit_load_str(r, v->data_name);
            }
            *out_reg = r;
        }
        return std::nullopt;
    }

    return error(std::format("Unexpected character '{}' in expr", *s));
}

std::optional<std::string> Parser::emit_unary(const char* s, const char** end, int* out_reg) {
    s = skip_ws(s);
    if (*s == '~') {
        int r;
        if (auto err = emit_unary(s + 1, end, &r)) {
            return err;
        }
        active_cg().emit_not(r);
        *out_reg = r;
        return std::nullopt;
    }
    return emit_atom(s, end, out_reg);
}

std::optional<std::string> Parser::emit_bitwise(const char* s, const char** end, int* out_reg) {
    int lreg;
    if (auto err = emit_unary(s, end, &lreg)) {
        return err;
    }
    s = skip_ws(*end);

    while (*s == '&' || *s == '|' || *s == '^' || (s[0] == '<' && s[1] == '<') ||
           (s[0] == '>' && s[1] == '>')) {
        char op0 = *s, op1 = *(s + 1);
        if (*s == '&' || *s == '|' || *s == '^') {
            s++;
        } else {
            s += 2;
        }

        s = skip_ws(s);
        int rreg;
        if (auto err = emit_unary(s, end, &rreg)) {
            ralloc_release(lreg);
            return err;
        }
        s = skip_ws(*end);

        if (op0 == '&') {
            active_cg().emit_and(lreg, rreg);
        } else if (op0 == '|') {
            active_cg().emit_or(lreg, rreg);
        } else if (op0 == '^') {
            active_cg().emit_xor(lreg, rreg);
        } else if (op0 == '<') {
            active_cg().emit_shl(lreg, rreg);
        } else {
            active_cg().emit_shr(lreg, rreg);
        }

        ralloc_release(rreg);
    }
    *end = s;
    *out_reg = lreg;
    return std::nullopt;
}

std::optional<std::string> Parser::emit_mul_expr(const char* s, const char** end, int* out_reg) {
    int lreg;
    if (auto err = emit_bitwise(s, end, &lreg)) {
        return err;
    }
    s = skip_ws(*end);

    while (*s == '*' || *s == '/' || *s == '%') {
        char op = *s++;
        s = skip_ws(s);
        int rreg;
        if (auto err = emit_bitwise(s, end, &rreg)) {
            ralloc_release(lreg);
            return err;
        }
        s = skip_ws(*end);

        if (op == '*') {
            active_cg().emit_mul(lreg, rreg);
        } else if (op == '/') {
            active_cg().emit_div(lreg, rreg);
        } else {
            active_cg().emit_mod(lreg, rreg);
        }

        ralloc_release(rreg);
    }
    *end = s;
    *out_reg = lreg;
    return std::nullopt;
}

std::optional<std::string> Parser::emit_expr(const char* s, int* out_reg) {
    const char* p = s;
    int lreg;
    if (auto err = emit_mul_expr(p, &p, &lreg)) {
        return err;
    }
    p = skip_ws(p);

    while (*p == '+' || *p == '-') {
        char op = *p++;
        p = skip_ws(p);
        int rreg;
        if (auto err = emit_mul_expr(p, &p, &rreg)) {
            ralloc_release(lreg);
            return err;
        }
        p = skip_ws(p);
        if (op == '+') {
            active_cg().emit_add(lreg, rreg);
        } else {
            active_cg().emit_sub(lreg, rreg);
        }
        ralloc_release(rreg);
    }
    *out_reg = lreg;
    return std::nullopt;
}

std::optional<std::string> Parser::emit_expr_p(const char* s, const char** end, int* out_reg) {
    const char* p = s;
    int lreg;
    if (auto err = emit_mul_expr(p, &p, &lreg)) {
        return err;
    }
    p = skip_ws(p);

    while (*p == '+' || *p == '-') {
        char op = *p++;
        p = skip_ws(p);
        int rreg;
        if (auto err = emit_mul_expr(p, &p, &rreg)) {
            ralloc_release(lreg);
            return err;
        }
        p = skip_ws(p);
        if (op == '+') {
            active_cg().emit_add(lreg, rreg);
        } else {
            active_cg().emit_sub(lreg, rreg);
        }
        ralloc_release(rreg);
    }
    *end = p;
    *out_reg = lreg;
    return std::nullopt;
}

std::optional<std::string> Parser::emit_condition(const char* s, const std::string& skip_label) {
    const char* p = s;

    for (const char* q = p; *q; q++) {
        bool left_ok = (q == p) || !(isalnum(static_cast<unsigned char>(q[-1])) || q[-1] == '_');
        bool right_ok = !(isalnum(static_cast<unsigned char>(q[2])) || q[2] == '_');
        if (starts_with_ci(std::string_view(q), "OR") && left_ok && right_ok) {
            std::string left(p, q - p);
            std::string next_label = make_label("_or_next");
            std::string pass_label = make_label("_or_pass");

            if (auto err = emit_condition(left.c_str(), next_label)) {
                return err;
            }
            active_cg().emit_jmp(pass_label);
            active_cg().emit_label(next_label);
            if (auto err = emit_condition(skip_ws(q + 2), skip_label)) {
                return err;
            }
            active_cg().emit_label(pass_label);
            return std::nullopt;
        }
    }

    while (true) {
        int lreg;
        if (auto err = emit_expr_p(p, &p, &lreg)) {
            return err;
        }
        p = skip_ws(p);

        std::string op;
        if (strncmp(p, "==", 2) == 0) {
            op = "==";
            p += 2;
        } else if (strncmp(p, "!=", 2) == 0) {
            op = "!=";
            p += 2;
        } else if (strncmp(p, "<=", 2) == 0) {
            op = "<=";
            p += 2;
        } else if (strncmp(p, ">=", 2) == 0) {
            op = ">=";
            p += 2;
        } else if (*p == '=') {
            op = "==";
            p++;
        } else if (*p == '<') {
            op = "<";
            p++;
        } else if (*p == '>') {
            op = ">";
            p++;
        } else {
            ralloc_release(lreg);
            return error(std::format("expected comparison operator near '{}'", p));
        }
        p = skip_ws(p);

        int rreg;
        if (auto err = emit_expr_p(p, &p, &rreg)) {
            ralloc_release(lreg);
            return err;
        }

        bool flip = (op == ">" || op == "<=");
        active_cg().emit_cmp(flip ? rreg : lreg, flip ? lreg : rreg);

        if (op == "==") {
            active_cg().emit_jne(skip_label);
        } else if (op == "!=") {
            active_cg().emit_je(skip_label);
        } else if (op == "<") {
            active_cg().emit_jge(skip_label);
        } else if (op == ">=") {
            active_cg().emit_jl(skip_label);
        } else if (op == ">") {
            active_cg().emit_jge(skip_label);
        } else if (op == "<=") {
            active_cg().emit_jl(skip_label);
        }

        ralloc_release(lreg);
        ralloc_release(rreg);

        const char* next = skip_ws(p);

        if (bool is_and = starts_with_ci(std::string_view(next), "AND")) {
            p = skip_ws(next + 3);
            continue;
        }
        break;
    }
    return std::nullopt;
}

std::optional<std::string> Parser::emit_write_values(const char* arg, std::string_view stmt_name,
                                                     bool to_stderr) {
    while (*arg) {
        arg = skip_ws(arg);

        // check if it's an identifier that names a string var
        if (isalpha(static_cast<unsigned char>(*arg)) || *arg == '_') {
            std::string name;
            const char* p = arg;
            while (*p && (isalnum(static_cast<unsigned char>(*p)) || *p == '_')) {
                name += *p++;
            }
            const char* after = skip_ws(p);
            if (*after == '\0' || *after == ',') {
                Variable* v = active_scope().find(name);
                if (v && v->type == VarType::Str) {
                    int r = ralloc_acquire();
                    if (r < 0) {
                        return error("out of scratch registers");
                    }
                    if (v->is_const) {
                        active_cg().emit_load_str(r, v->data_name);
                    } else if (v->is_global) {
                        active_cg().emit_load_global(r, v->name, v->name);
                    } else {
                        active_cg().emit_load_var(r, v->slot, v->name);
                    }
                    if (to_stderr) {
                        active_cg().emit_eprint_str(r);
                    } else {
                        active_cg().emit_print_str(r);
                    }
                    ralloc_release(r);
                    arg = after;
                    goto next_arg;
                }
            }
        }

        // string literal
        if (*arg == '"') {
            const char* str_end = strchr(arg + 1, '"');
            if (!str_end) {
                return error(std::format("unterminated string in {}", stmt_name));
            }
            std::string dname = std::format("_p{}", uid_++);
            active_cg().emit_data_str(dname, std::string(arg + 1, str_end - arg - 1));
            int r = ralloc_acquire();
            if (r < 0) {
                return error("out of scratch registers");
            }
            active_cg().emit_load_str(r, dname);
            if (to_stderr) {
                active_cg().emit_eprint_str(r);
            } else {
                active_cg().emit_print_str(r);
            }
            ralloc_release(r);
            arg = skip_ws(str_end + 1);
        } else {
            // numeric expression
            const char* expr_end = nullptr;
            int reg;
            if (auto err = emit_expr_p(arg, &expr_end, &reg)) {
                return err;
            }
            if (to_stderr) {
                active_cg().emit_eprint_reg(reg);
            } else {
                active_cg().emit_print_reg(reg);
            }
            ralloc_release(reg);
            arg = skip_ws(expr_end);
        }

    next_arg:
        if (*arg == ',') {
            arg = skip_ws(arg + 1);
            if (*arg == '\0') {
                return error(std::format("expected value after ',' in {}", stmt_name));
            }
            continue;
        }
        if (*arg != '\0') {
            return error(std::format("expected ',' between {} values", stmt_name));
        }
    }
    return std::nullopt;
}

} // namespace basic