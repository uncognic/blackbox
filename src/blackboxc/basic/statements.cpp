//
// Created by User on 2026-04-22.
//
#include "parser.hpp"
#include <cctype>
#include <cstring>
#include <format>
#include <print>

namespace basic {
std::optional<std::string> Parser::stmt_var(const std::string& s, bool is_global) {
    std::string body = s;

    if (starts_with_ci(body, "VAR ")) {
        body = trim(body.substr(4));
    }

    // array
    size_t start_bracket = body.find('[');
    if (start_bracket != std::string::npos) {
        std::string array_name = trim(body.substr(0, start_bracket));
        size_t close_bracket = body.find(']', start_bracket + 1);
        if (close_bracket == std::string::npos) {
            return error("expected ']' in array declaration");
        }
        std::string size_str =
            trim(body.substr(start_bracket + 1, close_bracket - start_bracket - 1));
        char* endptr;
        long size = strtol(size_str.c_str(), &endptr, 10);

        if (*endptr != '\0' || size <= 0) {
            return error(std::format("invalid array size '{}'", size_str));
        }

        if (array_name.empty()) {
            return error("expected array name");
        }
        if (arrays_.count(array_name)) {
            return error(std::format("array '{}' already declared", array_name));
        }
        arrays_[array_name] = ArrayInfo{heap_top_, static_cast<size_t>(size)};
        heap_top_ += static_cast<size_t>(size);
        active_cg().emit_grow(static_cast<int>(size));
        return std::nullopt;
    }

    size_t eq = body.find('=');
    if (eq == std::string::npos) {
        return error(std::format("expected VAR <name> = <value>"));
    }

    std::string name = trim(body.substr(0, eq));
    std::string rhs = trim(body.substr(eq + 1));
    if (name.empty()) {
        return error("expected variable name");
    }

    if (!rhs.empty() && rhs[0] == '"') {
        // string var
        const char* str_start = rhs.c_str() + 1;
        const char* str_end = strchr(str_start, '"');
        if (!str_end) {
            return error("unterminated string in VAR");
        }

        std::string dname = std::format("_s{}_{}", uid_++, name);
        std::string value(str_start, str_end - str_start);
        active_cg().emit_data_str(dname, value);

        Variable* v = active_scope().add_str(name, dname, false, is_global);
        int r = ralloc_acquire();
        if (r < 0) {
            return error("out of scratch registers");
        }
        active_cg().emit_load_str(r, dname);
        if (is_global) {
            active_cg().emit_store_global(r, v->name, name);
        } else {
            active_cg().emit_store_var(r, v->slot, name);
        }
        ralloc_release(r);

        if (debug_) {
            std::println("[BASIC] VAR string {} -> ${}", name, dname);
        }
    } else {
        // integer var
        if (active_scope().find(name) && !is_global) {
            return error(std::format("variable '{}' already defined", name));
        }

        Variable* v = active_scope().add_int(name, is_global);
        int ereg;
        if (auto err = emit_expr(rhs.c_str(), &ereg)) {
            return err;
        }
        if (is_global) {
            active_cg().emit_store_global(ereg, v->name, name);
        } else {
            active_cg().emit_store_var(ereg, v->slot, name);
        }
        ralloc_release(ereg);

        if (debug_) {
            std::println("[BASIC] VAR int {} -> slot {}", name, v->slot);
        }
    }

    return std::nullopt;
}

std::optional<std::string> Parser::stmt_const(const std::string& s) {
    std::string body = s;
    if (starts_with_ci(body, "CONST ")) {
        body = trim(body.substr(6));
    }

    size_t eq = body.find('=');
    if (eq == std::string::npos) {
        return error("expected CONST <name> = <value>");
    }

    std::string name = trim(body.substr(0, eq));
    std::string rhs = trim(body.substr(eq + 1));
    if (name.empty()) {
        return error("expected constant name");
    }

    if (active_scope().find(name)) {
        return error(std::format("'{}' already defined", name));
    }

    if (!rhs.empty() && rhs[0] == '"') {
        const char* str_start = rhs.c_str() + 1;
        const char* str_end = strchr(str_start, '"');
        if (!str_end) {
            return error("unterminated string in CONST");
        }

        std::string dname = std::format("_s{}_{}", uid_++, name);
        active_cg().emit_data_str(dname, std::string(str_start, str_end - str_start));
        active_scope().add_str(name, dname, true);

        if (debug_) {
            std::println("[BASIC] CONST string {} -> ${}", name, dname);
        }
    } else {
        Variable* v = active_scope().add_int(name);
        v->is_const = true;
        int ereg;
        if (auto err = emit_expr(rhs.c_str(), &ereg)) {
            return err;
        }
        active_cg().emit_store_var(ereg, v->slot, name);
        ralloc_release(ereg);

        if (debug_) {
            std::println("[BASIC] CONST int {} -> slot {}", name, v->slot);
        }
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_assign(const std::string& s) {
    // array write: arr[i] = expr
    size_t start_bracket = s.find('[');
    if (start_bracket != std::string::npos && start_bracket < 64) {
        std::string array_name = trim(s.substr(0, start_bracket));
        auto iterator = arrays_.find(array_name);
        if (iterator != arrays_.end()) {
            size_t close_bracket = s.find(']', start_bracket + 1);
            if (close_bracket == std::string::npos) {
                return error("expected ']' in array index");
            }
            size_t equals = s.find('=', close_bracket + 1);
            if (equals == std::string::npos) {
                return error("expected '=' after array index");
            }
            std::string index =
                trim(s.substr(start_bracket + 1, close_bracket - start_bracket - 1));
            std::string right = trim(s.substr(equals + 1));
            const ArrayInfo& array_info = iterator->second;

            int idx_reg;
            if (auto err = emit_expr(index.c_str(), &idx_reg)) {
                return err;
            }

            int addr_reg = ralloc_acquire();
            active_cg().emit_movi(addr_reg, static_cast<int32_t>(array_info.base));
            active_cg().emit_add(addr_reg, idx_reg);
            ralloc_release(idx_reg);

            int val_reg;
            if (auto err = emit_expr(right.c_str(), &val_reg)) {
                ralloc_release(addr_reg);
                return err;
            }
            active_cg().emit_heap_write(addr_reg, val_reg);
            ralloc_release(addr_reg);
            ralloc_release(val_reg);
            return std::nullopt;
        }
    }
    // find first = that isn't == != <= >=
    size_t eq = std::string::npos;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '=' && s[i + 1] != '=' &&
            (i == 0 || (s[i - 1] != '!' && s[i - 1] != '<' && s[i - 1] != '>'))) {
            eq = i;
            break;
        }
    }

    if (eq == std::string::npos || eq >= 64) {
        return std::optional<std::string>("__no_match__");
    }

    std::string name = trim(s.substr(0, eq));
    Variable* v = active_scope().find(name);
    if (!v) {
        return std::optional<std::string>("__no_match__");
    }

    if (v->is_const) {
        return error(std::format("cannot assign to CONST '{}'", name));
    }

    std::string rhs = trim(s.substr(eq + 1));

    if (v->type == VarType::Str) {
        if (rhs.empty() || rhs[0] != '"') {
            return error(std::format("expected string literal for '{}'", name));
        }
        const char* str_start = rhs.c_str() + 1;
        const char* str_end = strchr(str_start, '"');
        if (!str_end) {
            return error("unterminated string");
        }

        std::string dname = std::format("_s{}_{}", uid_++, name);
        active_cg().emit_data_str(dname, std::string(str_start, str_end - str_start));
        v->data_name = dname;

        int r = ralloc_acquire();
        if (r < 0) {
            return error("out of scratch registers");
        }
        active_cg().emit_load_str(r, dname);
        if (v->is_global) {
            active_cg().emit_store_global(r, v->name, name);
        } else {
            active_cg().emit_store_var(r, v->slot, name);
        }
        ralloc_release(r);

        if (debug_) {
            std::println("[BASIC] ASSIGN string {} -> ${}", name, dname);
        }
        return std::nullopt;
    }

    int ereg;
    if (auto err = emit_expr(rhs.c_str(), &ereg)) {
        return err;
    }

    if (v->is_ref) {
        int slot_r = ralloc_acquire();
        if (slot_r < 0) {
            ralloc_release(ereg);
            return error("out of scratch registers");
        }
        active_cg().emit_load_var(slot_r, v->slot, name);
        active_cg().emit_store_ref(slot_r, ereg);
        ralloc_release(ereg);
        ralloc_release(slot_r);
    } else if (v->is_global) {
        active_cg().emit_store_global(ereg, v->name, name);
        ralloc_release(ereg);
    } else {
        active_cg().emit_store_var(ereg, v->slot, name);
        ralloc_release(ereg);
    }

    if (debug_) {
        std::println("[BASIC] ASSIGN {} -> slot {}", name, v->slot);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_if(const std::string& s) {
    std::string cond = trim(s.substr(3));
    if (!cond.empty() && cond.back() == ':') {
        cond.pop_back();
    }

    Block b;
    b.kind = BlockKind::If;
    b.end_label = make_label("endif");
    b.else_label = make_label("else");

    if (auto err = emit_condition(cond.c_str(), b.else_label)) {
        return err;
    }
    block_stack_.push_back(b);

    if (debug_) {
        std::println("[BASIC] IF");
    }
    return std::nullopt;
}
std::optional<std::string> Parser::stmt_else_if(const std::string& s) {
    if (block_stack_.empty() || block_stack_.back().kind != BlockKind::If) {
        return error("ELSE IF without IF");
    }

    Block b = block_stack_.back();
    block_stack_.pop_back();

    active_cg().emit_jmp(b.end_label);
    active_cg().emit_label(b.else_label);

    // find the IF condition after ELSE IF
    size_t if_pos = s.find("IF ");
    if (if_pos == std::string::npos) {
        return error("malformed ELSE IF");
    }
    std::string cond = trim(s.substr(if_pos + 3));
    if (!cond.empty() && cond.back() == ':') {
        cond.pop_back();
    }

    Block nb;
    nb.kind = BlockKind::If;
    nb.end_label = b.end_label;
    nb.else_label = make_label("else");

    if (auto err = emit_condition(cond.c_str(), nb.else_label)) {
        return err;
    }
    block_stack_.push_back(nb);
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_else() {
    if (block_stack_.empty() || block_stack_.back().kind != BlockKind::If) {
        return error("ELSE without IF");
    }

    Block& b = block_stack_.back();
    b.has_else = true;
    active_cg().emit_jmp(b.end_label);
    active_cg().emit_label(b.else_label);

    if (debug_) {
        std::println("[BASIC] ELSE");
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_endif() {
    if (block_stack_.empty() || block_stack_.back().kind != BlockKind::If) {
        return error("ENDIF without IF");
    }

    Block b = block_stack_.back();
    block_stack_.pop_back();

    if (!b.has_else) {
        active_cg().emit_label(b.else_label);
    }
    active_cg().emit_label(b.end_label);

    if (debug_) {
        std::println("[BASIC] ENDIF");
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_while(const std::string& s) {
    std::string cond = trim(s.substr(6));
    if (!cond.empty() && cond.back() == ':') {
        cond.pop_back();
    }

    Block b;
    b.kind = BlockKind::While;
    b.loop_label = make_label("while");
    b.end_label = make_label("endwhile");

    active_cg().emit_label(b.loop_label);
    if (auto err = emit_condition(cond.c_str(), b.end_label)) {
        return err;
    }
    block_stack_.push_back(b);

    if (debug_) {
        std::println("[BASIC] WHILE");
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_endwhile() {
    if (block_stack_.empty() || block_stack_.back().kind != BlockKind::While) {
        return error("ENDWHILE without WHILE");
    }

    Block b = block_stack_.back();
    block_stack_.pop_back();

    active_cg().emit_jmp(b.loop_label);
    active_cg().emit_label(b.end_label);

    if (debug_) {
        std::println("[BASIC] ENDWHILE");
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_for(const std::string& s) {
    std::string body = trim(s.substr(4));
    if (!body.empty() && body.back() == ':') {
        body.pop_back();
    }

    bool inline_decl = false;
    if (starts_with_ci(body, "VAR ")) {
        inline_decl = true;
        body = trim(body.substr(4));
    }

    size_t eq = body.find('=');
    if (eq == std::string::npos) {
        return error("expected '=' in FOR");
    }

    std::string var_name = trim(body.substr(0, eq));
    std::string rest = trim(body.substr(eq + 1));

    // find TO keyword
    size_t to_pos = std::string::npos;
    {
        std::string upper = rest;
        for (auto& c : upper) {
            c = toupper(static_cast<unsigned char>(c));
        }
        to_pos = upper.find(" TO ");
        if (to_pos == std::string::npos) {
            return error("expected TO in FOR");
        }
    }

    std::string init_expr = trim(rest.substr(0, to_pos));
    std::string after_to = trim(rest.substr(to_pos + 4));

    // find optional STEP
    std::string limit_expr, step_expr = "1";
    {
        std::string upper = after_to;
        for (auto& c : upper) {
            c = toupper(static_cast<unsigned char>(c));
        }
        size_t step_pos = upper.find(" STEP ");
        if (step_pos != std::string::npos) {
            limit_expr = trim(after_to.substr(0, step_pos));
            step_expr = trim(after_to.substr(step_pos + 6));
        } else {
            limit_expr = after_to;
        }
    }

    Variable* v = active_scope().find(var_name);
    if (inline_decl) {
        if (v) {
            return error(std::format("'{}' already defined", var_name));
        }
        v = active_scope().add_int(var_name);
    } else {
        if (!v) {
            return error(std::format("undefined variable '{}'", var_name));
        }
    }
    if (v->is_const) {
        return error(std::format("cannot use CONST '{}' as FOR variable", var_name));
    }
    if (v->type != VarType::Int) {
        return error("FOR variable must be integer");
    }

    uint32_t limit_slot = active_scope().alloc_local_slot();
    uint32_t step_slot = active_scope().alloc_local_slot();

    {
        int r;
        if (auto err = emit_expr(init_expr.c_str(), &r)) {
            return err;
        }
        active_cg().emit_store_var(r, v->slot, var_name);
        ralloc_release(r);
    }
    {
        int r;
        if (auto err = emit_expr(limit_expr.c_str(), &r)) {
            return err;
        }
        active_cg().emit_store_var(r, limit_slot, "for-limit");
        ralloc_release(r);
    }
    {
        int r;
        if (auto err = emit_expr(step_expr.c_str(), &r)) {
            return err;
        }
        active_cg().emit_store_var(r, step_slot, "for-step");
        ralloc_release(r);
    }

    Block b;
    b.kind = BlockKind::For;
    b.loop_label = make_label("for");
    b.end_label = make_label("endfor");
    b.for_var_slot = v->slot;
    b.for_limit_slot = limit_slot;
    b.for_step_slot = step_slot;
    b.for_var_name = var_name;

    std::string neg_label = make_label("for_neg");
    std::string body_label = make_label("for_body");

    active_cg().emit_label(b.loop_label);

    int step_r = ralloc_acquire();
    int zero_r = ralloc_acquire();
    int var_r = ralloc_acquire();
    int lim_r = ralloc_acquire();
    if (step_r < 0 || zero_r < 0 || var_r < 0 || lim_r < 0) {
        return error("out of scratch registers");
    }

    active_cg().emit_load_var(step_r, step_slot, "for-step");
    active_cg().emit_movi(zero_r, 0);
    active_cg().emit_cmp(step_r, zero_r);
    active_cg().emit_jl(neg_label);

    active_cg().emit_load_var(var_r, v->slot, var_name);
    active_cg().emit_load_var(lim_r, limit_slot, "for-limit");
    active_cg().emit_cmp(lim_r, var_r);
    active_cg().emit_jl(b.end_label);
    active_cg().emit_jmp(body_label);

    active_cg().emit_label(neg_label);
    active_cg().emit_load_var(var_r, v->slot, var_name);
    active_cg().emit_load_var(lim_r, limit_slot, "for-limit");
    active_cg().emit_cmp(var_r, lim_r);
    active_cg().emit_jl(b.end_label);
    active_cg().emit_label(body_label);

    ralloc_release(step_r);
    ralloc_release(zero_r);
    ralloc_release(var_r);
    ralloc_release(lim_r);

    block_stack_.push_back(b);
    if (debug_) {
        std::println("[BASIC] FOR {}", var_name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_foreach(const std::string& s) {
    std::string body = trim(s.substr(7));
    if (!body.empty() && body.back() == ':') {
        body.pop_back();
    }

    bool inline_decl = false;
    if (starts_with_ci(body, "VAR ")) {
        inline_decl = true;
        body = trim(body.substr(4));
    }

    size_t in_pos;
    {
        std::string upper = body;
        for (auto& c : upper) {
            c = toupper(static_cast<unsigned char>(c));
        }
        in_pos = upper.find(" IN ");
        if (in_pos == std::string::npos) {
            return error("expected 'IN' in FOREACH");
        }
    }

    std::string var_name = trim(body.substr(0, in_pos));
    std::string arr_name = trim(body.substr(in_pos + 4));

    // resolve array
    auto it = arrays_.find(arr_name);
    if (it == arrays_.end()) {
        return error(std::format("undefined array '{}'", arr_name));
    }
    const ArrayInfo& arr = it->second;

    // variable for iterating
    Variable* v = active_scope().find(var_name);
    if (inline_decl) {
        if (v) {
            return error(std::format("'{}' already defined", var_name));
        }
        v = active_scope().add_int(var_name);
    } else if (!v) {
        return error(std::format("undefined variable '{}'", var_name));
    }

    if (v->is_const) {
        return error(std::format("cannot use CONST '{}' as FOREACH variable", var_name));
    }

    // allocate index
    uint32_t idx_slot = active_scope().alloc_local_slot();

    // idx = 0
    int r = ralloc_acquire();
    active_cg().emit_movi(r, 0);
    active_cg().emit_store_var(r, idx_slot, "foreach-idx");
    ralloc_release(r);

    Block b;
    b.kind = BlockKind::ForEach;
    b.loop_label = make_label("foreach");
    b.end_label = make_label("endforeach");
    b.foreach_idx_slot = idx_slot;
    b.foreach_elem_slot = v->slot;
    b.foreach_base = arr.base;
    b.foreach_length = arr.length;
    b.foreach_elem_name = var_name;

    active_cg().emit_label(b.loop_label);

    // load idx
    int idx_r = ralloc_acquire();
    int len_r = ralloc_acquire();

    // exit condition
    active_cg().emit_load_var(idx_r, idx_slot, "foreach-idx");
    active_cg().emit_movi(len_r, static_cast<int32_t>(arr.length));
    active_cg().emit_cmp(idx_r, len_r);
    active_cg().emit_jge(b.end_label);

    // addr = base + idx
    int addr_r = ralloc_acquire();
    active_cg().emit_movi(addr_r, static_cast<int32_t>(arr.base));
    active_cg().emit_add(addr_r, idx_r);

    // load element
    int val_r = ralloc_acquire();
    active_cg().emit_heap_read(val_r, addr_r);
    active_cg().emit_store_var(val_r, v->slot, var_name);

    ralloc_release(idx_r);
    ralloc_release(len_r);
    ralloc_release(addr_r);
    ralloc_release(val_r);

    block_stack_.push_back(b);

    return std::nullopt;
}

std::optional<std::string> Parser::stmt_next(const std::string& s) {
    if (block_stack_.empty() || (block_stack_.back().kind != BlockKind::For &&
                                 block_stack_.back().kind != BlockKind::ForEach)) {
        return error("NEXT without FOR/FOREACH");
    }

    std::string arg = trim(s.substr(4));
    Block b = block_stack_.back();
    block_stack_.pop_back();
    if (b.kind == BlockKind::For) {
        if (!arg.empty() && !equals_ci(arg, b.for_var_name)) {
            return error(std::format("NEXT variable '{}' does not match FOR variable '{}'", arg,
                                     b.for_var_name));
        }
    }
    if (b.kind == BlockKind::ForEach) {
        if (!arg.empty() && !equals_ci(arg, b.foreach_elem_name)) {
            return error(std::format("NEXT variable '{}' does not match FOR variable '{}'", arg,
                                     b.foreach_elem_name));
        }
    }

    int var_r = ralloc_acquire();
    int step_r = ralloc_acquire();
    if (var_r < 0 || step_r < 0) {
        return error("out of scratch registers");
    }

    if (b.kind == BlockKind::For) {
        active_cg().emit_load_var(var_r, b.for_var_slot, b.for_var_name);
        active_cg().emit_load_var(step_r, b.for_step_slot, "for-step");
        active_cg().emit_add(var_r, step_r);
        active_cg().emit_store_var(var_r, b.for_var_slot, b.for_var_name);
        active_cg().emit_jmp(b.loop_label);
        active_cg().emit_label(b.end_label);
    } else if (b.kind == BlockKind::ForEach) {
        active_cg().emit_inc_var(b.foreach_idx_slot);
        active_cg().emit_jmp(b.loop_label);
        active_cg().emit_label(b.end_label);
    }

    ralloc_release(var_r);
    ralloc_release(step_r);

    std::string name;
    if (b.kind == BlockKind::For) {
        name = b.for_var_name;
    } else if (b.kind == BlockKind::ForEach) {
        name = b.foreach_elem_name;
    }

    if (debug_) {
        std::println("[BASIC] NEXT {}", name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_break() {
    Block* target = find_loop_block();
    if (!target) {
        return error("BREAK outside loop");
    }
    active_cg().emit_jmp(target->end_label);
    if (debug_) {
        std::println("[BASIC] BREAK");
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_continue() {
    Block* target = find_loop_block();
    if (!target) {
        return error("CONTINUE outside loop");
    }

    if (target->kind == BlockKind::While) {
        active_cg().emit_jmp(target->loop_label);
    } else {
        int var_r = ralloc_acquire();
        int step_r = ralloc_acquire();
        if (var_r < 0 || step_r < 0) {
            return error("out of scratch registers");
        }
        active_cg().emit_load_var(var_r, target->for_var_slot, target->for_var_name);
        active_cg().emit_load_var(step_r, target->for_step_slot, "for-step");
        active_cg().emit_add(var_r, step_r);
        active_cg().emit_store_var(var_r, target->for_var_slot, target->for_var_name);
        active_cg().emit_jmp(target->loop_label);
        ralloc_release(var_r);
        ralloc_release(step_r);
    }
    if (debug_) {
        std::println("[BASIC] CONTINUE");
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_print(const std::string& s, bool to_stderr) {
    std::string_view kw = to_stderr ? "EPRINT" : "PRINT";
    std::string arg = trim(s.substr(kw.size()));

    if (!arg.empty()) {
        if (auto err = emit_write_values(arg.c_str(), kw, to_stderr)) {
            return err;
        }
    }

    if (to_stderr) {
        active_cg().emit_enewline();
    } else {
        active_cg().emit_newline();
    }

    if (debug_) {
        std::println("[BASIC] {}", kw);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_write(const std::string& s, bool to_stderr) {
    std::string_view kw = to_stderr ? "EWRITE" : "WRITE";
    std::string arg = trim(s.substr(kw.size()));
    if (!arg.empty()) {
        if (auto err = emit_write_values(arg.c_str(), kw, to_stderr)) {
            return err;
        }
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_call(const std::string& s) {
    std::string arg = trim(s.substr(5));

    std::string name;
    size_t i = 0;
    while (i < arg.size() && (isalnum(static_cast<unsigned char>(arg[i])) || arg[i] == '_')) {
        name += arg[i++];
    }
    while (i < arg.size() && arg[i] == '.') {
        i++;
        name += "__";
        while (i < arg.size() && (isalnum(static_cast<unsigned char>(arg[i])) || arg[i] == '_')) {
            name += arg[i++];
        }
    }

    while (i < arg.size() && isspace(static_cast<unsigned char>(arg[i]))) {
        i++;
    }

    if (i < arg.size() && arg[i] == '(') {
        std::string resolved_name = resolve_func_name(name);
        const FuncEntry* fd = find_func_entry(resolved_name);

        i++; // skip '('
        int arg_index = 0;

        while (i < arg.size() && arg[i] != ')') {
            while (i < arg.size() && isspace(static_cast<unsigned char>(arg[i]))) {
                i++;
            }
            bool is_ref = fd && arg_index < static_cast<int>(fd->param_is_ref.size()) &&
                          fd->param_is_ref[arg_index];

            if (is_ref) {
                std::string refname;
                while (i < arg.size() &&
                       (isalnum(static_cast<unsigned char>(arg[i])) || arg[i] == '_')) {
                    refname += arg[i++];
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
                const char* p = arg.c_str() + i;
                if (*p == '"') {
                    const char* str_end = strchr(p + 1, '"');
                    if (!str_end) {
                        return error("unterminated string in CALL");
                    }
                    std::string dname = std::format("_p{}", uid_++);
                    active_cg().emit_data_str(dname, std::string(p + 1, str_end - p - 1));
                    int r = ralloc_acquire();
                    if (r < 0) {
                        return error("out of scratch registers");
                    }
                    active_cg().emit_load_str(r, dname);
                    active_cg().emit_push(r);
                    ralloc_release(r);
                    i = str_end + 1 - arg.c_str();
                } else {
                    const char* end = nullptr;
                    int areg;
                    if (auto err = emit_expr_p(p, &end, &areg)) {
                        return err;
                    }
                    active_cg().emit_push(areg);
                    ralloc_release(areg);
                    i = end - arg.c_str();
                }
            }

            arg_index++;
            while (i < arg.size() && isspace(static_cast<unsigned char>(arg[i]))) {
                i++;
            }
            if (i < arg.size() && arg[i] == ',') {
                i++;
                continue;
            }
            if (i < arg.size() && arg[i] == ')') {
                break;
            }
            return error(std::format("expected ',' or ')' in CALL '{}'", name));
        }

        if (i >= arg.size() || arg[i] != ')') {
            return error(std::format("missing ')' in CALL '{}'", name));
        }

        active_cg().emit_call(std::format("__bbx_func_{}", resolved_name));
        if (debug_) {
            std::println("[BASIC] CALL {}(...)", resolved_name);
        }
        return std::nullopt;
    }

    // plain CALL
    active_cg().emit_call(name);
    if (debug_) {
        std::println("[BASIC] CALL {}", name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_return(const std::string& s) {
    std::string arg = trim(s.substr(6));

    if (!arg.empty()) {
        if (!current_func_) {
            return error("RETURN with value outside FUNC");
        }
        int r;
        if (auto err = emit_expr(arg.c_str(), &r)) {
            return err;
        }
        active_cg().emit_mov(0, r); // R00 = return value
        ralloc_release(r);
    }

    active_cg().emit_ret();
    if (debug_) {
        std::println("[BASIC] RETURN");
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_halt(const std::string& s) {
    std::string arg = trim(s.substr(4));
    uint8_t code = 0;

    if (arg.empty() || equals_ci(arg, "OK")) {
        code = 0;
    } else if (equals_ci(arg, "BAD")) {
        code = 1;
    } else {
        char* endp = nullptr;
        unsigned long v = strtoul(arg.c_str(), &endp, 0);
        if (!endp || *endp != '\0') {
            return error(std::format("invalid HALT operand '{}'", arg));
        }
        code = static_cast<uint8_t>(v);
    }

    active_cg().emit_halt(code);
    if (debug_) {
        std::println("[BASIC] HALT {}", arg);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_input(const std::string& s) {
    const char* p = s.c_str() + 6;
    p = skip_ws(p);

    // optional prompt string
    if (*p == '"') {
        const char* str_end = strchr(p + 1, '"');
        if (!str_end) {
            return error("unterminated string in INPUT");
        }
        std::string prompt(p, str_end - p + 1);
        if (auto err = emit_write_values(prompt.c_str(), "WRITE", false)) {
            return err;
        }
        p = skip_ws(str_end + 1);
        if (*p != ',') {
            return error("expected ',' after INPUT prompt");
        }
        p = skip_ws(p + 1);
    }

    std::string name(p);
    name = trim(name);
    Variable* v = active_scope().find(name);
    if (!v) {
        return error(std::format("undefined variable '{}'", name));
    }

    int r = ralloc_acquire();
    if (r < 0) {
        return error("out of scratch registers");
    }

    if (v->type == VarType::Str) {
        active_cg().emit_readstr(r);
        active_cg().emit_store_var(r, v->slot, name);
    } else {
        active_cg().emit_read(r);
        active_cg().emit_store_var(r, v->slot, name);
    }
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] INPUT {}", name);
    }
    return std::nullopt;
}

// ---------------------------------------------------------------

std::optional<std::string> Parser::stmt_goto(const std::string& s) {
    std::string name = trim(s.substr(5));
    active_cg().emit_jmp(name);
    if (debug_) {
        std::println("[BASIC] GOTO {}", name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_label(const std::string& s) {
    std::string name = trim(s.substr(6));
    active_cg().emit_label(name);
    if (debug_) {
        std::println("[BASIC] LABEL {}", name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_sleep(const std::string& s) {
    std::string arg = trim(s.substr(5));
    if (arg.empty()) {
        return error("expected SLEEP <expr>");
    }

    const char* end = nullptr;
    int r;
    if (auto err = emit_expr_p(arg.c_str(), &end, &r)) {
        return err;
    }
    if (*skip_ws(end) != '\0') {
        ralloc_release(r);
        return error("SLEEP takes a single expression");
    }
    active_cg().emit_sleep(r);
    ralloc_release(r);
    if (debug_) {
        std::println("[BASIC] SLEEP {}", arg);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_random(const std::string& s) {
    const char* p = skip_ws(s.c_str() + 6);
    std::string name;
    while (*p && (isalnum(static_cast<unsigned char>(*p)) || *p == '_')) {
        name += *p++;
    }
    p = skip_ws(p);

    Variable* v = active_scope().find(name);
    if (!v) {
        return error(std::format("undefined variable '{}'", name));
    }
    if (v->is_const) {
        return error(std::format("cannot assign to CONST '{}'", name));
    }
    if (v->type != VarType::Int) {
        return error("RANDOM target must be integer");
    }

    int r = ralloc_acquire();
    if (r < 0) {
        return error("out of scratch registers");
    }

    if (*p == ',') {
        p = skip_ws(p + 1);
        std::string rest(p);
        size_t comma = rest.find(',');
        if (comma == std::string::npos) {
            return error("expected RANDOM <n>, <min>, <max>");
        }
        std::string min_s = trim(rest.substr(0, comma));
        std::string max_s = trim(rest.substr(comma + 1));
        active_cg().emit_rand_range(r, min_s, max_s);
    } else {
        active_cg().emit_rand(r);
    }

    if (v->is_global) {
        active_cg().emit_store_global(r, v->name, name);
    } else {
        active_cg().emit_store_var(r, v->slot, name);
    }
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] RANDOM {}", name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_inc(const std::string& s) {
    std::string name = trim(s.substr(3));
    for (size_t dot = name.find('.'); dot != std::string::npos; dot = name.find('.', dot + 2)) {
        name.replace(dot, 1, "__");
    }
    Variable* v = active_scope().find(name);

    if (!v && name.find("__") != std::string::npos) {
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
    if (v->is_const) {
        return error(std::format("cannot INC CONST '{}'", name));
    }
    if (v->type != VarType::Int) {
        return error("cannot INC non-integer");
    }

    if (v->is_global) {
        active_cg().emit_inc_global(v->name);
    } else {
        active_cg().emit_inc_var(v->slot);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_dec(const std::string& s) {
    std::string name = trim(s.substr(3));
    for (size_t dot = name.find('.'); dot != std::string::npos; dot = name.find('.', dot + 2)) {
        name.replace(dot, 1, "__");
    }
    Variable* v = active_scope().find(name);
    if (!v && name.find("__") != std::string::npos) {
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
    if (v->is_const) {
        return error(std::format("cannot DEC CONST '{}'", name));
    }
    if (v->type != VarType::Int) {
        return error("cannot DEC non-integer");
    }

    if (v->is_global) {
        active_cg().emit_dec_global(v->name);
    } else {
        active_cg().emit_dec_var(v->slot);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_exec(const std::string& s) {
    const char* p = skip_ws(s.c_str() + 4);
    if (*p != '"') {
        return error("expected EXEC \"<cmd>\", <var>");
    }

    const char* str_end = strchr(p + 1, '"');
    if (!str_end) {
        return error("missing closing quote for EXEC");
    }

    std::string cmd(p + 1, str_end - p - 1);
    p = skip_ws(str_end + 1);

    int r = ralloc_acquire();
    if (r < 0) {
        return error("out of scratch registers");
    }
    active_cg().emit_exec(cmd, r);

    if (*p == ',') {
        p = skip_ws(p + 1);
        std::string varname(p);
        varname = trim(varname);
        Variable* dv = active_scope().find(varname);
        if (!dv) {
            return error(std::format("undefined variable '{}'", varname));
        }
        if (dv->is_const) {
            return error(std::format("cannot assign to CONST '{}'", varname));
        }
        if (dv->type != VarType::Int) {
            return error("EXEC destination must be integer");
        }
        if (dv->is_global) {
            active_cg().emit_store_global(r, dv->name, varname);
        } else {
            active_cg().emit_store_var(r, dv->slot, varname);
        }
    }

    ralloc_release(r);
    if (debug_) {
        std::println("[BASIC] EXEC \"{}\"", cmd);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_fopen(const std::string& s) {
    // FOPEN <mode>, <handle>, "<file>"
    std::string arg = trim(s.substr(5));
    size_t c1 = arg.find(',');
    if (c1 == std::string::npos) {
        return error("expected FOPEN <mode>, <handle>, \"<file>\"");
    }
    std::string mode = trim(arg.substr(0, c1));
    std::string rest = trim(arg.substr(c1 + 1));
    size_t c2 = rest.find(',');
    if (c2 == std::string::npos) {
        return error("expected FOPEN <mode>, <handle>, \"<file>\"");
    }
    std::string handle_name = trim(rest.substr(0, c2));
    std::string file_arg = trim(rest.substr(c2 + 1));

    if (file_arg.empty() || file_arg[0] != '"') {
        return error("expected quoted filename in FOPEN");
    }
    size_t q2 = file_arg.find('"', 1);
    if (q2 == std::string::npos) {
        return error("unterminated filename in FOPEN");
    }
    std::string filename = file_arg.substr(1, q2 - 1);

    Variable* v = active_scope().find(handle_name);
    if (!v) {
        return error(std::format("undefined variable '{}'", handle_name));
    }
    if (v->type != VarType::Int) {
        return error("file handle must be integer");
    }

    auto fd = alloc_file_handle_fd(handle_name);
    if (!fd) {
        return error("too many file handles");
    }

    active_cg().emit_fopen(mode, *fd, filename);

    int r = ralloc_acquire();
    if (r < 0) {
        return error("out of scratch registers");
    }
    active_cg().emit_movi(r, static_cast<int32_t>(*fd));
    active_cg().emit_store_var(r, v->slot, handle_name);
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] FOPEN {} -> {}", filename, handle_name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_fclose(const std::string& s) {
    std::string handle_name = trim(s.substr(6));
    auto fd = get_file_handle_fd(handle_name);
    if (!fd) {
        return error(std::format("undefined file handle '{}'", handle_name));
    }
    active_cg().emit_fclose(*fd);
    if (debug_) {
        std::println("[BASIC] FCLOSE {}", handle_name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_fread(const std::string& s) {
    std::string arg = trim(s.substr(5));
    size_t comma = arg.find(',');
    if (comma == std::string::npos) {
        return error("expected FREAD <handle>, <var>");
    }
    std::string handle_name = trim(arg.substr(0, comma));
    std::string var_name = trim(arg.substr(comma + 1));

    auto fd = get_file_handle_fd(handle_name);
    if (!fd) {
        return error(std::format("undefined file handle '{}'", handle_name));
    }

    Variable* v = active_scope().find(var_name);
    if (!v) {
        return error(std::format("undefined variable '{}'", var_name));
    }
    if (v->type != VarType::Int) {
        return error("FREAD target must be integer");
    }

    int r = ralloc_acquire();
    if (r < 0) {
        return error("out of scratch registers");
    }
    active_cg().emit_fread(*fd, r);
    active_cg().emit_store_var(r, v->slot, var_name);
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] FREAD {} -> {}", handle_name, var_name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_fwrite(const std::string& s) {
    std::string arg = trim(s.substr(6));
    size_t comma = arg.find(',');
    if (comma == std::string::npos) {
        return error("expected FWRITE <handle>, <expr>");
    }
    std::string handle_name = trim(arg.substr(0, comma));
    std::string expr_str = trim(arg.substr(comma + 1));

    auto fd = get_file_handle_fd(handle_name);
    if (!fd) {
        return error(std::format("undefined file handle '{}'", handle_name));
    }

    const char* end = nullptr;
    int r;
    if (auto err = emit_expr_p(expr_str.c_str(), &end, &r)) {
        return err;
    }
    active_cg().emit_fwrite(*fd, r);
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] FWRITE {}", handle_name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_fseek(const std::string& s) {
    std::string arg = trim(s.substr(5));
    size_t comma = arg.find(',');
    if (comma == std::string::npos) {
        return error("expected FSEEK <handle>, <expr>");
    }
    std::string handle_name = trim(arg.substr(0, comma));
    std::string expr_str = trim(arg.substr(comma + 1));

    auto fd = get_file_handle_fd(handle_name);
    if (!fd) {
        return error(std::format("undefined file handle '{}'", handle_name));
    }

    const char* end = nullptr;
    int r;
    if (auto err = emit_expr_p(expr_str.c_str(), &end, &r)) {
        return err;
    }
    active_cg().emit_fseek(*fd, r);
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] FSEEK {}", handle_name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_fprint(const std::string& s) {
    std::string arg = trim(s.substr(6));
    size_t comma = arg.find(',');
    if (comma == std::string::npos) {
        return error("expected FPRINT <handle>, <value>");
    }
    std::string handle_name = trim(arg.substr(0, comma));
    std::string val_str = trim(arg.substr(comma + 1));

    auto fd = get_file_handle_fd(handle_name);
    if (!fd) {
        return error(std::format("undefined file handle '{}'", handle_name));
    }

    if (!val_str.empty() && val_str[0] == '"') {
        size_t q2 = val_str.find('"', 1);
        if (q2 == std::string::npos) {
            return error("unterminated string in FPRINT");
        }
        std::string text = val_str.substr(1, q2 - 1);
        int r = ralloc_acquire();
        if (r < 0) {
            return error("out of scratch registers");
        }
        for (unsigned char c : text) {
            active_cg().emit_movi(r, static_cast<int32_t>(c));
            active_cg().emit_fwrite(*fd, r);
        }
        active_cg().emit_movi(r, 10);
        active_cg().emit_fwrite(*fd, r);
        ralloc_release(r);
    } else {
        const char* end = nullptr;
        int r;
        if (auto err = emit_expr_p(val_str.c_str(), &end, &r)) {
            return err;
        }
        active_cg().emit_fwrite(*fd, r);
        int nr = ralloc_acquire();
        if (nr >= 0) {
            active_cg().emit_movi(nr, 10);
            active_cg().emit_fwrite(*fd, nr);
            ralloc_release(nr);
        }
        ralloc_release(r);
    }

    if (debug_) {
        std::println("[BASIC] FPRINT {}", handle_name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_getarg(const std::string& s) {
    std::string arg = trim(s.substr(6));
    size_t comma = arg.find(',');
    if (comma == std::string::npos) {
        return error("expected GETARG <var>, <index>");
    }
    std::string var_name = trim(arg.substr(0, comma));
    std::string idx_str = trim(arg.substr(comma + 1));

    Variable* v = active_scope().find(var_name);
    if (!v) {
        return error(std::format("undefined variable '{}'", var_name));
    }
    if (v->type != VarType::Str) {
        return error("GETARG target must be string");
    }

    char* endp = nullptr;
    uint32_t idx = static_cast<uint32_t>(strtoul(idx_str.c_str(), &endp, 0));
    if (!endp || *endp != '\0') {
        return error(std::format("invalid GETARG index '{}'", idx_str));
    }

    int r = ralloc_acquire();
    if (r < 0) {
        return error("out of scratch registers");
    }
    active_cg().emit_getarg(r, idx);
    active_cg().emit_store_var(r, v->slot, var_name);
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] GETARG {}, {}", var_name, idx);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_getargc(const std::string& s) {
    std::string name = trim(s.substr(7));
    Variable* v = active_scope().find(name);
    if (!v) {
        return error(std::format("undefined variable '{}'", name));
    }
    if (v->type != VarType::Int) {
        return error("GETARGC target must be integer");
    }

    int r = ralloc_acquire();
    if (r < 0) {
        return error("out of scratch registers");
    }
    active_cg().emit_getargc(r);
    active_cg().emit_store_var(r, v->slot, name);
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] GETARGC {}", name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_getenv(const std::string& s) {
    std::string arg = trim(s.substr(6));
    size_t comma = arg.find(',');
    if (comma == std::string::npos) {
        return error("expected GETENV <var>, <envname>");
    }
    std::string var_name = trim(arg.substr(0, comma));
    std::string env_arg = trim(arg.substr(comma + 1));

    // env name may be quoted or bare
    std::string envname;
    if (!env_arg.empty() && env_arg[0] == '"') {
        size_t q2 = env_arg.find('"', 1);
        if (q2 == std::string::npos) {
            return error("unterminated string in GETENV");
        }
        envname = env_arg.substr(1, q2 - 1);
    } else {
        envname = env_arg;
    }

    Variable* v = active_scope().find(var_name);
    if (!v) {
        return error(std::format("undefined variable '{}'", var_name));
    }
    if (v->type != VarType::Str) {
        return error("GETENV target must be string");
    }

    int r = ralloc_acquire();
    if (r < 0) {
        return error("out of scratch registers");
    }
    active_cg().emit_getenv(r, envname);
    active_cg().emit_store_var(r, v->slot, var_name);
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] GETENV {}, {}", var_name, envname);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_getkey(const std::string& s) {
    std::string name = trim(s.substr(6));
    Variable* v = active_scope().find(name);
    if (!v) {
        return error(std::format("undefined variable '{}'", name));
    }
    if (v->type != VarType::Int) {
        return error("GETKEY target must be integer");
    }

    int r = ralloc_acquire();
    if (r < 0) {
        return error("out of scratch registers");
    }
    active_cg().emit_getkey(r);
    active_cg().emit_store_var(r, v->slot, name);
    ralloc_release(r);

    if (debug_) {
        std::println("[BASIC] GETKEY {}", name);
    }
    return std::nullopt;
}

std::optional<std::string> Parser::stmt_clrscr() {
    active_cg().emit_clrscr();
    if (debug_) {
        std::println("[BASIC] CLRSCR");
    }
    return std::nullopt;
}

} // namespace basic