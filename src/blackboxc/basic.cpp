#include "basic.hpp"
#include "../utils/string_utils.hpp"

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string>
namespace {
std::string mangle(const std::string& ns, const char* name) {
    return ns.empty() ? std::string(name) : (ns + "::" + name);
}
void copy_cstr(char* dst, size_t dst_size, const char* src) {
    if (dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

std::string trim_copy(const std::string& s) {
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
void reg_name(int reg, char* buf) {
    snprintf(buf, 4, "R%02d", reg);
}

const char* skip_ws(const char* s) {
    while (*s && isspace(static_cast<unsigned char>(*s))) {
        s++;
    }
    return s;
}

size_t skip_ws(const std::string& s, size_t pos) {
    while (pos < s.size() && isspace(static_cast<unsigned char>(s[pos]))) {
        pos++;
    }
    return pos;
}

bool parse_identifier(const char* p, const char** next, std::string& out) {
    size_t n = 0;
    while (isalnum(static_cast<unsigned char>(p[n])) || p[n] == '_') {
        n++;
    }
    if (n == 0) {
        return false;
    }
    out.assign(p, n);
    if (next) {
        *next = p + n;
    }
    return true;
}

bool parse_identifier(const std::string& s, size_t& pos, std::string& out) {
    size_t start = pos;
    while (pos < s.size() && (isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
        pos++;
    }
    if (pos == start) {
        return false;
    }
    out = s.substr(start, pos - start);
    return true;
}

bool parse_quoted_string(const std::string& s, size_t& pos, std::string& out) {
    if (pos >= s.size() || s[pos] != '"') {
        return false;
    }
    size_t end = s.find('"', pos + 1);
    if (end == std::string::npos) {
        return false;
    }
    out = s.substr(pos + 1, end - pos - 1);
    pos = end + 1;
    return true;
}

bool parse_file_mode(const std::string& s, size_t& pos, std::string& mode_out) {
    pos = skip_ws(s, pos);
    if (pos >= s.size()) {
        return false;
    }
    if (s[pos] == '"') {
        return parse_quoted_string(s, pos, mode_out);
    }
    size_t start = pos;
    while (pos < s.size() && (isalnum(static_cast<unsigned char>(s[pos])) || s[pos] == '_')) {
        pos++;
    }
    if (pos == start || pos - start >= 8) {
        return false;
    }
    mode_out = s.substr(start, pos - start);
    return true;
}

const char* find_keyword_token(const char* s, const char* kw) {
    size_t n = strlen(kw);
    for (const char* p = s; *p; p++) {
        if (!blackbox::tools::starts_with_ci(p, kw)) {
            continue;
        }
        int left_ok = (p == s) || !(isalnum(static_cast<unsigned char>(p[-1])) || p[-1] == '_');
        int right_ok = !(isalnum(static_cast<unsigned char>(p[n])) || p[n] == '_');
        if (left_ok && right_ok) {
            return p;
        }
    }
    return nullptr;
}

const char* block_kind_name(BlockKind kind) {
    switch (kind) {
        case BLOCK_IF:
            return "IF";
        case BLOCK_WHILE:
            return "WHILE";
        case BLOCK_FOR:
            return "FOR";
        default:
            return "UNKNOWN";
    }
}

int get_file_handle_index(const std::vector<FileHandle>& handles, const char* name) {
    for (size_t i = 0; i < handles.size(); i++) {
        if (handles[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}
} // namespace
struct RegGuard {
    RegAlloc* ra;
    int reg;

    explicit RegGuard(RegAlloc* ra_) : ra(ra_), reg(-1) {
        for (int i = 0; i < SCRATCH_COUNT; i++) {
            if (!(ra->used & (1u << i))) {
                ra->used |= (1u << i);
                reg = SCRATCH_MIN + i;
                return;
            }
        }
    }

    ~RegGuard() { release(); }

    void release() {
        if (reg >= SCRATCH_MIN && reg <= SCRATCH_MAX) {
            ra->used &= ~(1u << (reg - SCRATCH_MIN));
            reg = -1;
        }
    }

    bool ok() const { return reg >= 0; }
    operator int() const { return reg; }

    RegGuard(const RegGuard&) = delete;
    RegGuard& operator=(const RegGuard&) = delete;
};

#define EMIT_DATA(cs, fmt, ...)                                                                    \
    do {                                                                                           \
        if ((cs)->emit_data(fmt, ##__VA_ARGS__))                                                   \
            return 1;                                                                              \
    } while (0)

#define EMIT_CODE(cs, fmt, ...)                                                                    \
    do {                                                                                           \
        if ((cs)->emit_code_comment(NULL, fmt, ##__VA_ARGS__))                                     \
            return 1;                                                                              \
    } while (0)

#define EMIT_CODE_META(cs, meta, fmt, ...)                                                         \
    do {                                                                                           \
        if ((cs)->emit_code_comment((meta), fmt, ##__VA_ARGS__))                                   \
            return 1;                                                                              \
    } while (0)

CompilerState::CompilerState() : ra() {
    symbol_table.next_slot = 0;
    symbol_table.next_data_id = 0;
    ra.used = 0;
    emit_ctx[0] = '\0';
}

Variable* CompilerState::sym_find(const char* name) {
    if (!current_namespace.empty()) {
        std::string mangled = current_namespace + "::" + name;
        for (auto& v : symbol_table.vars) {
            if (std::strcmp(v.name, mangled.c_str()) == 0) {
                return &v;
            }
        }
    }
    for (auto& v : symbol_table.vars) {
        if (std::strcmp(v.name, name) == 0) {
            return &v;
        }
    }
    if (parent_ns_state) {
        return parent_ns_state->sym_find(name);
    }
    return nullptr;
}

Variable* CompilerState::sym_add_int(const char* name) {
    std::string name_mangled = mangle(current_namespace, name);
    if (Variable* existing = sym_find(name_mangled.c_str())) {
        return existing;
    }
    Variable v;
    copy_cstr(v.name, sizeof(v.name), name_mangled.c_str());
    v.type = VAR_INT;
    v.is_const = 0;
    v.is_ref = false;
    v.slot = symbol_table.next_slot++;
    v.data_name[0] = '\0';
    symbol_table.vars.push_back(v);
    return &symbol_table.vars.back();
}

Variable* CompilerState::sym_add_str(const char* name, const char* data_name, int is_const) {
    std::string name_mangled = mangle(current_namespace, name);
    if (Variable* existing = sym_find(name_mangled.c_str())) {
        return existing;
    }
    Variable v;
    copy_cstr(v.name, sizeof(v.name), name_mangled.c_str());
    v.type = VAR_STR;
    v.is_const = is_const ? 1 : 0;
    v.is_ref = false;
    v.slot = symbol_table.next_slot++;
    copy_cstr(v.data_name, sizeof(v.data_name), data_name);
    symbol_table.vars.push_back(v);
    return &symbol_table.vars.back();
}
Variable* CompilerState::sym_add_ref(const char* name) {
    std::string name_mangled = mangle(current_namespace, name);
    if (Variable* existing = sym_find(name_mangled.c_str())) {
        return existing;
    }
    symbol_table.vars.emplace_back();
    Variable* var = &symbol_table.vars.back();
    std::memset(var, 0, sizeof(*var));
    copy_cstr(var->name, sizeof(var->name), name_mangled.c_str());
    var->type = VAR_INT;
    var->is_ref = true;
    var->slot = symbol_table.next_slot++;
    return var;
}

int CompilerState::ralloc_acquire() {
    for (int i = 0; i < SCRATCH_COUNT; i++) {
        if (!(ra.used & (1u << i))) {
            ra.used |= (1u << i);
            return SCRATCH_MIN + i;
        }
    }
    return -1;
}

void CompilerState::ralloc_release(int reg) {
    if (reg >= SCRATCH_MIN && reg <= SCRATCH_MAX) {
        ra.used &= ~(1u << (reg - SCRATCH_MIN));
    }
}

void CompilerState::set_emit_context(const char* stmt) {
    copy_cstr(emit_ctx, sizeof(emit_ctx), stmt);
}

int CompilerState::emit_data(const char* fmt, ...) {
    char buf[8192];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len < 0 || len >= static_cast<int>(sizeof(buf))) {
        return 1;
    }
    ob.data_sec.append(buf, static_cast<size_t>(len));
    ob.data_sec.push_back('\n');
    return 0;
}

int CompilerState::emit_code_comment(const char* detail, const char* fmt, ...) {
    char buf[8192];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len < 0 || len >= static_cast<int>(sizeof(buf))) {
        return 1;
    }
    if (detail && detail[0]) {
        ob.code_sec.append("    ; ");
        ob.code_sec.append(detail);
        ob.code_sec.push_back('\n');
    }
    ob.code_sec.append(buf, static_cast<size_t>(len));
    ob.code_sec.push_back('\n');
    return 0;
}

int CompilerState::get_file_handle_fd(const char* name, uint8_t* out_fd) {
    int index = get_file_handle_index(file_handles, name);
    if (index < 0) {
        std::println(stderr, "Undefined file handle '{}' on line {}", name, lineno);
        return 1;
    }
    if (out_fd) {
        *out_fd = file_handles[index].fd;
    }
    return 0;
}

int CompilerState::alloc_file_handle_fd(const char* name, uint8_t* out_fd) {
    int index = get_file_handle_index(file_handles, name);
    if (index >= 0) {
        if (out_fd) {
            *out_fd = file_handles[index].fd;
        }
        return 0;
    }
    if (next_file_handle == 0xFF) {
        std::println(stderr, "Too many file handles on line {}", lineno);
        return 1;
    }
    FileHandle fh;
    fh.name = name;
    fh.fd = next_file_handle++;
    file_handles.push_back(fh);
    if (out_fd) {
        *out_fd = fh.fd;
    }
    return 0;
}

void CompilerState::bstack_push(const Block& b) {
    block_stack.items.push_back(b);
}

Block* CompilerState::bstack_peek() {
    if (block_stack.items.empty()) {
        return nullptr;
    }
    return &block_stack.items.back();
}

Block CompilerState::bstack_pop() {
    if (block_stack.items.empty()) {
        return Block{};
    }
    Block b = block_stack.items.back();
    block_stack.items.pop_back();
    return b;
}

int CompilerState::emit_atom(const char* s, const char** end, int* out_reg) {
    s = skip_ws(s);

    if (*s == '(') {
        s++;
        if (emit_expr(s, out_reg)) {
            return 1;
        }
        s = skip_ws(*end);
        if (*s != ')') {
            std::println(stderr, "Expected ')' in expression");
            return 1;
        }
        *end = s + 1;
        return 0;
    }

    if (*s == '-' || isdigit(static_cast<unsigned char>(*s))) {
        char* endptr;
        int32_t val = static_cast<int32_t>(strtol(s, &endptr, 10));
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of registers");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    MOVI %s, %d", rn, val);
        *out_reg = rg.reg;
        rg.reg = -1; // transfer ownership to caller
        *end = endptr;
        return 0;
    }

    if (isalpha(static_cast<unsigned char>(*s)) || *s == '_') {
        std::string name;
        const char* next = s;
        if (!parse_identifier(s, &next, name)) {
            std::println(stderr, "Expression error: expected identifier");
            return 1;
        }

        // function call?
        const char* after_name = skip_ws(next);
        if (*after_name == '(' && funcs) {
            const FuncDef* fd = nullptr;
            for (auto& f : *funcs) {
                if (f.name == name) {
                    fd = &f;
                    break;
                }
            }
            // make the name unique by replacing namespace dots with __
            while (*skip_ws(next) == '.') {
                next = skip_ws(next) + 1;
                std::string part;
                if (!parse_identifier(next, &next, part)) {
                    std::println(stderr, "Expression error line {}: expected identifier after '.'",
                                 lineno);
                    return 1;
                }
                name += "__" + part;
            }

            if (fd) {
                const char* p = skip_ws(after_name + 1);
                int arg_index = 0;
                while (*p && *p != ')') {
                    if (arg_index < static_cast<int>(fd->param_is_ref.size()) &&
                        fd->param_is_ref[arg_index]) {
                        // ref param - push caller's slot number
                        std::string refname;
                        const char* ref_end = p;
                        if (!parse_identifier(p, &ref_end, refname)) {
                            std::println(stderr,
                                         "Error line {}: ref param requires a variable name",
                                         lineno);
                            return 1;
                        }
                        Variable* rv = sym_find(refname.data());
                        if (!rv) {
                            std::println(stderr, "Undefined variable '{}' on line {}", refname,
                                         lineno);
                            return 1;
                        }
                        p = ref_end;
                        RegGuard rg(&ra);
                        if (!rg.ok()) {
                            std::println(stderr, "Out of scratch registers");
                            return 1;
                        }
                        char rn[4];
                        reg_name(rg, rn);
                        EMIT_CODE(this, "    MOVI %s, %u ; ref slot for %s", rn, rv->slot,
                                  refname.data());
                        EMIT_CODE(this, "    PUSH %s ; ref arg for call to %s", rn, name.data());
                    } else {
                        // regular parameter
                        p = skip_ws(p);
                        if (*p == '"') {
                            const char* str_end = strchr(p + 1, '"');
                            if (!str_end) {
                                std::println(
                                    stderr,
                                    "Expression error line {}: unterminated string in call to '{}'",
                                    lineno, name);
                                return 1;
                            }
                            char data_name[64];
                            snprintf(data_name, sizeof(data_name), "_p%lu", uid++);
                            EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name,
                                      static_cast<int>(str_end - p - 1), p + 1);
                            RegGuard rg(&ra);
                            if (!rg.ok()) {
                                std::println(stderr, "Out of scratch registers");
                                return 1;
                            }
                            char rn[4];
                            reg_name(rg, rn);
                            EMIT_CODE(this, "    LOADSTR $%s, %s", data_name, rn);
                            EMIT_CODE(this, "    PUSH %s ; string argument for call to %s", rn,
                                      name.data());
                            p = str_end + 1;
                        } else {
                            int arg_reg;
                            if (emit_expr_p(p, &p, &arg_reg)) {
                                return 1;
                            }
                            char arn[4];
                            reg_name(arg_reg, arn);
                            EMIT_CODE(this, "    PUSH %s ; argument for call to %s", arn,
                                      name.data());
                            ralloc_release(arg_reg);
                        }
                    }
                    arg_index++;
                    p = skip_ws(p);
                    if (*p == ',') {
                        p = skip_ws(p + 1);
                        continue;
                    }
                    if (*p == ')') {
                        break;
                    }
                    std::println(stderr,
                                 "Expression error line {}: expected ',' or ')' in call to '{}'",
                                 lineno, name);
                    return 1;
                }
                if (*p != ')') {
                    std::println(stderr, "Expression error line {}: expected ')' in call to '{}'",
                                 lineno, name);
                    return 1;
                }
                *end = p + 1;
                EMIT_CODE(this, "    CALL __bbx_func_%s", name.data());
                RegGuard rg(&ra);
                if (!rg.ok()) {
                    std::println(stderr, "Out of scratch registers");
                    return 1;
                }
                char rn[4];
                reg_name(rg, rn);
                EMIT_CODE(this, "    MOV %s, R00 ; return value from %s", rn, name.data());
                *out_reg = rg.reg;
                rg.reg = -1;
                return 0;
            }
        }

        // not a function
        *end = next;
        Variable* v = sym_find(name.data());
        if (!v) {
            std::println(stderr, "Undefined variable '{}'", name);
            return 1;
        }

        if (v->is_ref) {
            RegGuard slot_r(&ra);
            if (!slot_r.ok()) {
                std::println(stderr, "Out of scratch registers");
                return 1;
            }
            RegGuard val_r(&ra);
            if (!val_r.ok()) {
                std::println(stderr, "Out of scratch registers");
                return 1;
            }
            char srn[4], vrn[4];
            reg_name(slot_r, srn);
            reg_name(val_r, vrn);
            EMIT_CODE_META(this, v->name, "    LOADVAR %s, %u", srn, v->slot);
            EMIT_CODE(this, "    LOADREF %s, %s", vrn, srn);
            *out_reg = val_r.reg;
            val_r.reg = -1;
        } else {
            RegGuard rg(&ra);
            if (!rg.ok()) {
                std::println(stderr, "Out of scratch registers");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            if (v->type == VAR_INT || !v->is_const) {
                EMIT_CODE_META(this, v->name, "    LOADVAR %s, %u", rn, v->slot);
            } else {
                EMIT_CODE(this, "    LOADSTR $%s, %s", v->data_name, rn);
            }
            *out_reg = rg.reg;
            rg.reg = -1;
        }
        return 0;
    }

    std::println(stderr, "Expression error: unexpected character '{}'", *s);
    return 1;
}

int CompilerState::emit_unary(const char* s, const char** end, int* out_reg) {
    s = skip_ws(s);
    if (*s == '~') {
        int reg;
        if (emit_unary(s + 1, end, &reg)) {
            return 1;
        }
        char rn[4];
        reg_name(reg, rn);
        EMIT_CODE(this, "    NOT %s", rn);
        *out_reg = reg;
        return 0;
    }
    return emit_atom(s, end, out_reg);
}

int CompilerState::emit_bitwise(const char* s, const char** end, int* out_reg) {
    int lreg;
    if (emit_unary(s, end, &lreg)) {
        return 1;
    }
    s = skip_ws(*end);

    while (*s == '&' || *s == '|' || *s == '^' || (s[0] == '<' && s[1] == '<') ||
           (s[0] == '>' && s[1] == '>')) {
        char op[3] = {0};
        if (*s == '&' || *s == '|' || *s == '^') {
            op[0] = *s++;
        } else if (s[0] == '<') {
            op[0] = '<';
            op[1] = '<';
            s += 2;
        } else {
            op[0] = '>';
            op[1] = '>';
            s += 2;
        }

        s = skip_ws(s);
        int rreg;
        if (emit_unary(s, end, &rreg)) {
            ralloc_release(lreg);
            return 1;
        }
        s = skip_ws(*end);

        char ln[4], rn[4];
        reg_name(lreg, ln);
        reg_name(rreg, rn);
        if (op[0] == '&') {
            EMIT_CODE(this, "    AND %s, %s", ln, rn);
        } else if (op[0] == '|') {
            EMIT_CODE(this, "    OR %s, %s", ln, rn);
        } else if (op[0] == '^') {
            EMIT_CODE(this, "    XOR %s, %s", ln, rn);
        } else if (op[0] == '<') {
            EMIT_CODE(this, "    SHL %s, %s", ln, rn);
        } else {
            EMIT_CODE(this, "    SHR %s, %s", ln, rn);
        }
        ralloc_release(rreg);
    }
    *end = s;
    *out_reg = lreg;
    return 0;
}

int CompilerState::emit_mul(const char* s, const char** end, int* out_reg) {
    int lreg;
    if (emit_bitwise(s, end, &lreg)) {
        return 1;
    }
    s = skip_ws(*end);

    while (*s == '*' || *s == '/' || *s == '%') {
        char op = *s++;
        s = skip_ws(s);
        int rreg;
        if (emit_bitwise(s, end, &rreg)) {
            ralloc_release(lreg);
            return 1;
        }
        s = skip_ws(*end);

        char ln[4], rn[4];
        reg_name(lreg, ln);
        reg_name(rreg, rn);
        if (op == '*') {
            EMIT_CODE(this, "    MUL %s, %s", ln, rn);
        } else if (op == '/') {
            EMIT_CODE(this, "    DIV %s, %s", ln, rn);
        } else {
            EMIT_CODE(this, "    MOD %s, %s", ln, rn);
        }
        ralloc_release(rreg);
    }
    *end = s;
    *out_reg = lreg;
    return 0;
}

int CompilerState::emit_expr(const char* s, int* out_reg) {
    const char* p = s;
    int lreg;
    if (emit_mul(p, &p, &lreg)) {
        return 1;
    }
    p = skip_ws(p);

    while (*p == '+' || *p == '-') {
        char op = *p++;
        p = skip_ws(p);
        int rreg;
        if (emit_mul(p, &p, &rreg)) {
            ralloc_release(lreg);
            return 1;
        }
        p = skip_ws(p);
        char ln[4], rn[4];
        reg_name(lreg, ln);
        reg_name(rreg, rn);
        if (op == '+') {
            EMIT_CODE(this, "    ADD %s, %s", ln, rn);
        } else {
            EMIT_CODE(this, "    SUB %s, %s", ln, rn);
        }
        ralloc_release(rreg);
    }
    *out_reg = lreg;
    return 0;
}

int CompilerState::emit_expr_p(const char* s, const char** end, int* out_reg) {
    const char* p = s;
    int lreg;
    if (emit_mul(p, &p, &lreg)) {
        return 1;
    }
    p = skip_ws(p);

    while (*p == '+' || *p == '-') {
        char op = *p++;
        p = skip_ws(p);
        int rreg;
        if (emit_mul(p, &p, &rreg)) {
            ralloc_release(lreg);
            return 1;
        }
        p = skip_ws(p);
        char ln[4], rn[4];
        reg_name(lreg, ln);
        reg_name(rreg, rn);
        if (op == '+') {
            EMIT_CODE(this, "    ADD %s, %s", ln, rn);
        } else {
            EMIT_CODE(this, "    SUB %s, %s", ln, rn);
        }
        ralloc_release(rreg);
    }
    *end = p;
    *out_reg = lreg;
    return 0;
}

int CompilerState::emit_condition(const char* s, const char* skip_label) {
    const char* p = s;

    for (const char* q = p; *q; q++) {
        if (blackbox::tools::starts_with_ci(q, "OR") &&
            (q == p || !(isalnum(static_cast<unsigned char>(q[-1])) || q[-1] == '_')) &&
            !(isalnum(static_cast<unsigned char>(q[2])) || q[2] == '_')) {

            std::string left(p, static_cast<size_t>(q - p));
            char next_or_label[64], pass_label[64];
            snprintf(next_or_label, sizeof(next_or_label), "_or_next_%lu", uid++);
            snprintf(pass_label, sizeof(pass_label), "_or_pass_%lu", uid++);

            if (emit_condition(left.data(), next_or_label)) {
                return 1;
            }
            EMIT_CODE(this, "    JMP %s", pass_label);
            EMIT_CODE(this, ".%s:", next_or_label);
            if (emit_condition(skip_ws(q + 2), skip_label)) {
                return 1;
            }
            EMIT_CODE(this, ".%s:", pass_label);
            return 0;
        }
    }

    while (true) {
        int lreg;
        if (emit_expr_p(p, &p, &lreg)) {
            return 1;
        }
        p = skip_ws(p);

        char op[3] = {0};
        if (strncmp(p, "==", 2) == 0) {
            op[0] = '=';
            op[1] = '=';
            p += 2;
        } else if (strncmp(p, "!=", 2) == 0) {
            op[0] = '!';
            op[1] = '=';
            p += 2;
        } else if (strncmp(p, "<=", 2) == 0) {
            op[0] = '<';
            op[1] = '=';
            p += 2;
        } else if (strncmp(p, ">=", 2) == 0) {
            op[0] = '>';
            op[1] = '=';
            p += 2;
        } else if (*p == '=') {
            op[0] = '=';
            op[1] = '=';
            p++;
        } else if (*p == '<') {
            op[0] = '<';
            p++;
        } else if (*p == '>') {
            op[0] = '>';
            p++;
        } else {
            std::println(stderr, "Condition error: expected comparison operator near '{}'", p);
            ralloc_release(lreg);
            return 1;
        }
        p = skip_ws(p);

        int rreg;
        if (emit_expr_p(p, &p, &rreg)) {
            ralloc_release(lreg);
            return 1;
        }

        char ln[4], rn[4];
        reg_name(lreg, ln);
        reg_name(rreg, rn);

        const char* next = skip_ws(p);
        int is_and = blackbox::tools::starts_with_ci(next, "AND") &&
                     !(isalnum(static_cast<unsigned char>(next[3])) || next[3] == '_');

        bool flip = (strcmp(op, ">") == 0 || strcmp(op, "<=") == 0);
        EMIT_CODE(this, "    CMP %s, %s", flip ? rn : ln, flip ? ln : rn);

        if (strcmp(op, "==") == 0) {
            EMIT_CODE(this, "    JNE %s", skip_label);
        } else if (strcmp(op, "!=") == 0) {
            EMIT_CODE(this, "    JE  %s", skip_label);
        } else if (strcmp(op, "<") == 0) {
            EMIT_CODE(this, "    JGE %s", skip_label);
        } else if (strcmp(op, ">=") == 0) {
            EMIT_CODE(this, "    JL  %s", skip_label);
        } else if (strcmp(op, ">") == 0) {
            EMIT_CODE(this, "    JGE %s", skip_label);
        } else if (strcmp(op, "<=") == 0) {
            EMIT_CODE(this, "    JL  %s", skip_label);
        }

        ralloc_release(lreg);
        ralloc_release(rreg);
        if (is_and) {
            p = skip_ws(next + 3);
            continue;
        }
        break;
    }
    return 0;
}

int CompilerState::emit_write_values(const char* arg, const char* stmt_name, int to_stderr) {
    while (*arg) {
        const char* arg_start = skip_ws(arg);

        if (isalpha(static_cast<unsigned char>(*arg_start)) || *arg_start == '_') {
            std::string name;
            const char* p = arg_start;
            if (!parse_identifier(arg_start, &p, name)) {
                std::println(stderr, "Syntax error line {}: expected identifier in {}", lineno,
                             stmt_name);
                return 1;
            }
            if (*skip_ws(p) == '\0' || *skip_ws(p) == ',') {
                Variable* v = sym_find(name.data());
                if (v && v->type == VAR_STR) {
                    RegGuard rg(&ra);
                    if (!rg.ok()) {
                        std::println(stderr, "Out of scratch registers");
                        return 1;
                    }
                    char rn[4];
                    reg_name(rg, rn);
                    if (v->is_const) {
                        EMIT_CODE(this, "    LOADSTR $%s, %s", v->data_name, rn);
                    } else {
                        EMIT_CODE_META(this, v->name, "    LOADVAR %s, %u", rn, v->slot);
                    }
                    if (to_stderr) {
                        EMIT_CODE(this, "    EPRINTSTR %s", rn);
                    } else {
                        EMIT_CODE(this, "    PRINTSTR %s", rn);
                    }
                    if (debug) {
                        std::println("[BASIC] {} {}", stmt_name, name);
                    }
                    arg = skip_ws(p);
                    goto next_arg;
                }
            }
        }

        if (*arg == '"') {
            const char* str_start = arg + 1;
            const char* str_end = strchr(str_start, '"');
            if (!str_end) {
                std::println(stderr, "Syntax error line {}: unterminated string in {}", lineno,
                             stmt_name);
                return 1;
            }
            char data_name[64];
            snprintf(data_name, sizeof(data_name), "_p%lu", uid++);
            EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name,
                      static_cast<int>(str_end - str_start), str_start);
            RegGuard rg(&ra);
            if (!rg.ok()) {
                std::println(stderr, "Out of scratch registers");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            EMIT_CODE(this, "    LOADSTR $%s, %s", data_name, rn);
            if (to_stderr) {
                EMIT_CODE(this, "    EPRINTSTR %s", rn);
            } else {
                EMIT_CODE(this, "    PRINTSTR %s", rn);
            }
            if (debug) {
                std::println("[BASIC] {} \"{:.{}}\"", stmt_name, str_start,
                             static_cast<int>(str_end - str_start));
            }
            arg = skip_ws(str_end + 1);
        } else {
            const char* expr_end = nullptr;
            int reg;
            if (emit_expr_p(arg, &expr_end, &reg)) {
                return 1;
            }
            char rn[4];
            reg_name(reg, rn);
            if (to_stderr) {
                EMIT_CODE(this, "    EPRINTREG %s", rn);
            } else {
                EMIT_CODE(this, "    PRINTREG %s", rn);
            }
            ralloc_release(reg);
            if (debug) {
                std::println("[BASIC] {} {:.{}}", stmt_name, arg, static_cast<int>(expr_end - arg));
            }
            arg = skip_ws(expr_end);
        }

    next_arg:
        if (*arg == ',') {
            arg = skip_ws(arg + 1);
            if (*arg == '\0') {
                std::println(stderr, "Syntax error line {}: expected value after ',' in {}", lineno,
                             stmt_name);
                return 1;
            }
            continue;
        }
        if (*arg != '\0') {
            std::println(stderr, "Syntax error line {}: expected ',' between {} values", lineno,
                         stmt_name);
            return 1;
        }
    }
    return 0;
}

int CompilerState::compile_line(char* s) {

    // ENTRY point directive
    if (blackbox::tools::starts_with_ci(s, "@ENTRY")) {
        const char* tail = skip_ws(s + 6);
        if (*tail != '\0') {
            std::println(stderr, "Syntax error line {}: unexpected tokens after @ENTRY", lineno);
            return 1;
        }
        if (in_func) {
            std::println(stderr, "Error line {}: @ENTRY is not allowed inside FUNC", lineno);
            return 1;
        }
        if (entry_point_declared) {
            std::println(stderr, "Error line {}: multiple @ENTRY directives are not allowed",
                         lineno);
            return 1;
        }
        EMIT_CODE(this, ".__bbx_basic_main:");
        entry_point_declared = true;
        if (debug) {
            std::println("[BASIC] @ENTRY");
        }
        return 0;
    }

    // CONST
    if (blackbox::tools::starts_with_ci(s, "CONST ")) {
        const char* eq = strchr(s + 6, '=');
        if (!eq) {
            std::println(stderr, "Syntax error line {}: expected CONST <n> = <value>", lineno);
            return 1;
        }
        std::string name = trim_copy(std::string(s + 6, static_cast<size_t>(eq - (s + 6))));
        if (name.empty()) {
            std::println(stderr, "Syntax error line {}: expected CONST <n> = <value>", lineno);
            return 1;
        }
        if (sym_find(name.data())) {
            std::println(stderr, "Error line {}: variable '{}' already defined", lineno, name);
            return 1;
        }
        std::string rhs = trim_copy(eq + 1);
        if (!rhs.empty() && rhs[0] == '"') {
            const char* str_start = rhs.data() + 1;
            const char* str_end = strchr(str_start, '"');
            if (!str_end) {
                std::println(stderr, "Syntax error line {}: unterminated string literal", lineno);
                return 1;
            }
            char data_name[64];
            snprintf(data_name, sizeof(data_name), "_s%lu_%s", uid++, name.data());
            EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name,
                      static_cast<int>(str_end - str_start), str_start);
            sym_add_str(name.data(), data_name, 1);
            if (debug) {
                std::println("[BASIC] CONST string {} -> ${}", name, data_name);
            }
        } else {
            Variable* v = sym_add_int(name.data());
            v->is_const = 1;
            int ereg;
            if (emit_expr(rhs.data(), &ereg)) {
                return 1;
            }
            char ern[4];
            reg_name(ereg, ern);
            EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", ern, v->slot);
            ralloc_release(ereg);
            if (debug) {
                std::println("[BASIC] CONST int {} -> slot {}", name, v->slot);
            }
        }
        return 0;
    }

    // VAR
    if (blackbox::tools::starts_with_ci(s, "VAR ")) {
        const char* eq = strchr(s + 4, '=');
        if (!eq) {
            std::println(stderr, "Syntax error line {}: expected VAR <n> = <value>", lineno);
            return 1;
        }
        std::string name = trim_copy(std::string(s + 4, static_cast<size_t>(eq - (s + 4))));
        std::string rhs = trim_copy(eq + 1);
        if (name.empty()) {
            std::println(stderr, "Syntax error line {}: expected VAR <n> = <value>", lineno);
            return 1;
        }
        if (!rhs.empty() && rhs[0] == '"') {
            const char* str_start = rhs.data() + 1;
            const char* str_end = strchr(str_start, '"');
            if (!str_end) {
                std::println(stderr, "Syntax error line {}: unterminated string", lineno);
                return 1;
            }
            char data_name[64];
            snprintf(data_name, sizeof(data_name), "_s%lu_%s", uid++, name.data());
            EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name,
                      static_cast<int>(str_end - str_start), str_start);
            Variable* v = sym_add_str(name.data(), data_name, 0);
            RegGuard rg(&ra);
            if (!rg.ok()) {
                std::println(stderr, "Out of scratch registers");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            EMIT_CODE(this, "    LOADSTR $%s, %s", data_name, rn);
            EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
            if (debug) {
                std::println("[BASIC] VAR string {} -> ${}", name, data_name);
            }
        } else {
            if (sym_find(name.data())) {
                std::println(stderr, "Error line {}: variable '{}' already defined", lineno, name);
                return 1;
            }
            Variable* v = sym_add_int(name.data());
            int ereg;
            if (emit_expr(rhs.data(), &ereg)) {
                return 1;
            }
            char ern[4];
            reg_name(ereg, ern);
            EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", ern, v->slot);
            ralloc_release(ereg);
            if (debug) {
                std::println("[BASIC] VAR int {} -> slot {}", name, v->slot);
            }
        }
        return 0;
    }

    // assignment: name = expr
    {
        char* eq = nullptr;
        for (char* p = s; *p; p++) {
            if (*p == '=' && *(p + 1) != '=' &&
                (p == s || (*(p - 1) != '!' && *(p - 1) != '<' && *(p - 1) != '>'))) {
                eq = p;
                break;
            }
        }
        if (eq) {
            size_t lhslen = static_cast<size_t>(eq - s);
            if (lhslen < 64) {
                std::string name = trim_copy(std::string(s, lhslen));
                Variable* v = sym_find(name.data());
                if (v) {
                    if (v->is_const) {
                        std::println(stderr, "Error line {}: cannot assign to CONST '{}'", lineno,
                                     name);
                        return 1;
                    }
                    std::string rhs = trim_copy(eq + 1);
                    if (v->type == VAR_STR) {
                        if (rhs.empty() || rhs[0] != '"') {
                            std::println(stderr,
                                         "Type error line {}: expected string literal for '{}'",
                                         lineno, name);
                            return 1;
                        }
                        const char* str_start = rhs.data() + 1;
                        const char* str_end = strchr(str_start, '"');
                        if (!str_end) {
                            std::println(stderr, "Syntax error line {}: unterminated string",
                                         lineno);
                            return 1;
                        }
                        if (*skip_ws(str_end + 1) != '\0') {
                            std::println(
                                stderr,
                                "Syntax error line {}: unexpected trailing tokens after string",
                                lineno);
                            return 1;
                        }
                        char data_name[64];
                        snprintf(data_name, sizeof(data_name), "_s%lu_%s", uid++, name.data());
                        EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name,
                                  static_cast<int>(str_end - str_start), str_start);
                        copy_cstr(v->data_name, sizeof(v->data_name), data_name);
                        RegGuard rg(&ra);
                        char rn[4];
                        reg_name(rg, rn);
                        EMIT_CODE(this, "    LOADSTR $%s, %s", data_name, rn);
                        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
                        if (debug) {
                            std::println("[BASIC] ASSIGN string {} -> ${}", name, data_name);
                        }
                        return 0;
                    }
                    if (v->type != VAR_INT) {
                        std::println(
                            stderr,
                            "Type error line {}: cannot assign to string '{}' with numeric expr",
                            lineno, name);
                        return 1;
                    }
                    int ereg;
                    if (emit_expr(rhs.data(), &ereg)) {
                        return 1;
                    }
                    if (v->is_ref) {
                        RegGuard slot_r(&ra);
                        if (!slot_r.ok()) {
                            ralloc_release(ereg);
                            std::println(stderr, "Out of scratch registers");
                            return 1;
                        }
                        char srn[4], ern[4];
                        reg_name(slot_r, srn);
                        reg_name(ereg, ern);
                        EMIT_CODE_META(this, name.data(), "    LOADVAR %s, %u", srn, v->slot);
                        EMIT_CODE(this, "    STOREREF %s, %s", srn, ern);
                        ralloc_release(ereg);
                    } else {
                        char ern[4];
                        reg_name(ereg, ern);
                        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", ern, v->slot);
                        ralloc_release(ereg);
                    }
                    if (debug) {
                        std::println("[BASIC] ASSIGN {} -> slot {}", name, v->slot);
                    }
                    return 0;
                }
            }
        }
    }

    // IF
    if (blackbox::tools::starts_with_ci(s, "IF ")) {
        size_t slen = strlen(s);
        if (s[slen - 1] == ':') {
            s[slen - 1] = '\0';
        }
        char end_label[64], else_label[64];
        snprintf(end_label, sizeof(end_label), "endif_%lu", uid);
        snprintf(else_label, sizeof(else_label), "else_%lu", uid);
        uid++;
        Block b;
        b.kind = BLOCK_IF;
        b.has_else = 0;
        snprintf(b.end_label, sizeof(b.end_label), ".%s", end_label);
        snprintf(b.else_label, sizeof(b.else_label), ".%s", else_label);
        b.loop_label[0] = '\0';
        if (emit_condition(s + 3, b.else_label + 1)) {
            return 1;
        }
        bstack_push(b);
        if (debug) {
            std::println("[BASIC] IF condition, skip to {} if false", b.else_label);
        }
        return 0;
    }

    // ELSE IF
    if (blackbox::tools::starts_with_ci(s, "ELSE ")) {
        const char* p = skip_ws(s + 5);
        if (blackbox::tools::starts_with_ci(p, "IF ")) {
            Block b = bstack_pop();
            if (b.kind != BLOCK_IF) {
                std::println(stderr, "Error line {}: ELSE IF without IF", lineno);
                return 1;
            }
            EMIT_CODE(this, "    JMP %s", b.end_label + 1);
            EMIT_CODE(this, "%s:", b.else_label);
            const char* if_start = p + 3;
            size_t if_len = strlen(if_start);
            if (if_len > 0 && if_start[if_len - 1] == ':') {
                if_len--;
            }
            std::string if_stmt(if_start, if_len);
            char new_else_label[64];
            snprintf(new_else_label, sizeof(new_else_label), "else_%lu", uid++);
            if (emit_condition(if_stmt.data(), new_else_label)) {
                return 1;
            }
            Block nb;
            nb.kind = BLOCK_IF;
            nb.has_else = 0;
            snprintf(nb.end_label, sizeof(nb.end_label), "%s", b.end_label);
            snprintf(nb.else_label, sizeof(nb.else_label), ".%s", new_else_label);
            nb.loop_label[0] = '\0';
            bstack_push(nb);
            if (debug) {
                std::println("[BASIC] ELSE IF condition, else to {}, exit to {}", nb.else_label,
                             nb.end_label);
            }
            return 0;
        }
    }

    // ELSE:
    if (blackbox::tools::equals_ci(s, "ELSE:")) {
        Block* b = bstack_peek();
        if (!b || b->kind != BLOCK_IF) {
            std::println(stderr, "Error line {}: ELSE without IF", lineno);
            return 1;
        }
        b->has_else = 1;
        EMIT_CODE(this, "    JMP %s", b->end_label + 1);
        EMIT_CODE(this, "%s:", b->else_label);
        if (debug) {
            std::println("[BASIC] ELSE");
        }
        return 0;
    }

    // ENDIF
    if (blackbox::tools::equals_ci(s, "ENDIF")) {
        Block b = bstack_pop();
        if (b.kind != BLOCK_IF) {
            std::println(stderr, "Error line {}: ENDIF without IF", lineno);
            return 1;
        }
        if (!b.has_else) {
            EMIT_CODE(this, "%s:", b.else_label);
        }
        EMIT_CODE(this, "%s:", b.end_label);
        if (debug) {
            std::println("[BASIC] ENDIF");
        }
        return 0;
    }

    // WHILE
    if (blackbox::tools::starts_with_ci(s, "WHILE ")) {
        size_t slen = strlen(s);
        if (s[slen - 1] == ':') {
            s[slen - 1] = '\0';
        }
        char loop_label[64], end_label[64];
        snprintf(loop_label, sizeof(loop_label), "while_%lu", uid);
        snprintf(end_label, sizeof(end_label), "endwhile_%lu", uid);
        uid++;
        Block b;
        b.kind = BLOCK_WHILE;
        b.has_else = 0;
        b.else_label[0] = '\0';
        snprintf(b.loop_label, sizeof(b.loop_label), ".%s", loop_label);
        snprintf(b.end_label, sizeof(b.end_label), ".%s", end_label);
        EMIT_CODE(this, "%s:", b.loop_label);
        if (emit_condition(s + 6, b.end_label + 1)) {
            return 1;
        }
        bstack_push(b);
        if (debug) {
            std::println("[BASIC] WHILE -> top={} end={}", b.loop_label, b.end_label);
        }
        return 0;
    }

    // ENDWHILE
    if (blackbox::tools::equals_ci(s, "ENDWHILE")) {
        Block b = bstack_pop();
        if (b.kind != BLOCK_WHILE) {
            std::println(stderr, "Error line {}: ENDWHILE without WHILE", lineno);
            return 1;
        }
        EMIT_CODE(this, "    JMP %s", b.loop_label + 1);
        EMIT_CODE(this, "%s:", b.end_label);
        if (debug) {
            std::println("[BASIC] ENDWHILE");
        }
        return 0;
    }

    if (blackbox::tools::starts_with_ci(s, "EWRITE")) {
        const char* arg = s + 6;
        if (*arg != '\0' && !isspace(static_cast<unsigned char>(*arg))) {
            std::println(stderr, "Syntax error line {}: expected EWRITE [<value>[, ...]]", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg != '\0' && emit_write_values(arg, "EWRITE", 1)) {
            return 1;
        }
        return 0;
    }
    if (blackbox::tools::starts_with_ci(s, "WRITE")) {
        const char* arg = s + 5;
        if (*arg != '\0' && !isspace(static_cast<unsigned char>(*arg))) {
            std::println(stderr, "Syntax error line {}: expected WRITE [<value>[, ...]]", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg != '\0' && emit_write_values(arg, "WRITE", 0)) {
            return 1;
        }
        return 0;
    }
    if (blackbox::tools::starts_with_ci(s, "EPRINT")) {
        const char* arg = s + 6;
        if (*arg != '\0' && !isspace(static_cast<unsigned char>(*arg))) {
            std::println(stderr, "Syntax error line {}: expected EPRINT [<value>[, ...]]", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg != '\0' && emit_write_values(arg, "EPRINT", 1)) {
            return 1;
        }
        {
            RegGuard rg(&ra);
            if (!rg.ok()) {
                std::println(stderr, "Out of scratch registers");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            EMIT_CODE(this, "    MOVI %s, 10", rn);
            EMIT_CODE(this, "    EPRINTCHAR %s", rn);
        }
        if (debug) {
            std::println("[BASIC] EPRINT <newline>");
        }
        return 0;
    }
    if (blackbox::tools::starts_with_ci(s, "PRINT")) {
        const char* arg = s + 5;
        if (*arg != '\0' && !isspace(static_cast<unsigned char>(*arg))) {
            std::println(stderr, "Syntax error line {}: expected PRINT [<value>[, ...]]", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg != '\0' && emit_write_values(arg, "PRINT", 0)) {
            return 1;
        }
        {
            RegGuard rg(&ra);
            if (!rg.ok()) {
                std::println(stderr, "Out of scratch registers");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            EMIT_CODE(this, "    MOVI %s, 10", rn);
            EMIT_CODE(this, "    PRINTCHAR %s", rn);
        }
        if (debug) {
            std::println("[BASIC] PRINT <newline>");
        }
        return 0;
    }

    // SLEEP
    if (blackbox::tools::starts_with_ci(s, "SLEEP")) {
        const char* arg = s + 5;
        if (*arg != '\0' && !isspace(static_cast<unsigned char>(*arg))) {
            std::println(stderr, "Syntax error line {}: expected SLEEP <expr>", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg == '\0') {
            std::println(stderr, "Syntax error line {}: expected SLEEP <expr>", lineno);
            return 1;
        }
        const char* expr_end = nullptr;
        int reg;
        if (emit_expr_p(arg, &expr_end, &reg)) {
            return 1;
        }
        if (*skip_ws(expr_end) != '\0') {
            ralloc_release(reg);
            std::println(stderr, "Syntax error line {}: SLEEP takes a single expression", lineno);
            return 1;
        }
        char rn[4];
        reg_name(reg, rn);
        EMIT_CODE(this, "    SLEEP %s", rn);
        ralloc_release(reg);
        if (debug) {
            std::println("[BASIC] SLEEP {}", arg);
        }
        return 0;
    }

    // HALT
    if (blackbox::tools::starts_with_ci(s, "HALT")) {
        const char* halt_suffix = s + 4;
        if (*halt_suffix != '\0' && !std::isspace(static_cast<unsigned char>(*halt_suffix))) {
            std::println(stderr, "Syntax error line {}: expected HALT [OK|BAD|<number>]", lineno);
            return 1;
        }
        std::string arg = trim_copy(halt_suffix);
        if (arg.empty()) {
            EMIT_CODE(this, "    HALT");
            if (debug) {
                std::println("[BASIC] HALT");
            }
            return 0;
        }
        size_t tok_len = 0;
        while (tok_len < arg.size() && !std::isspace(static_cast<unsigned char>(arg[tok_len]))) {
            tok_len++;
        }
        std::string token = arg.substr(0, tok_len);
        if (!trim_copy(arg.substr(tok_len)).empty()) {
            std::println(stderr, "Syntax error line {}: HALT takes at most one operand", lineno);
            return 1;
        }
        if (blackbox::tools::equals_ci(token.data(), "OK")) {
            EMIT_CODE(this, "    HALT OK");
        } else if (blackbox::tools::equals_ci(token.data(), "BAD")) {
            EMIT_CODE(this, "    HALT BAD");
        } else {
            char* endp = nullptr;
            unsigned long v = strtoul(token.data(), &endp, 0);
            if (!endp || *endp != '\0') {
                std::println(stderr, "Syntax error line {}: invalid HALT operand '{}'", lineno,
                             token);
                return 1;
            }
            EMIT_CODE(this, "    HALT %lu", v);
        }
        if (debug) {
            std::println("[BASIC] HALT {}", token);
        }
        return 0;
    }

    // LABEL
    if (blackbox::tools::starts_with_ci(s, "LABEL ")) {
        std::string name = trim_copy(s + 6);
        EMIT_CODE(this, ".%s:", name.data());
        if (debug) {
            std::println("[BASIC] LABEL {}", name);
        }
        return 0;
    }

    // GOTO
    if (blackbox::tools::starts_with_ci(s, "GOTO ")) {
        std::string name = trim_copy(s + 5);
        EMIT_CODE(this, "    JMP %s", name.data());
        if (debug) {
            std::println("[BASIC] GOTO {}", name);
        }
        return 0;
    }

    // CALL
    if (blackbox::tools::starts_with_ci(s, "CALL ")) {
        const char* arg = skip_ws(s + 5);

        std::string name;
        const char* p = arg;
        if (!parse_identifier(p, &p, name)) {
            std::println(stderr, "Syntax error line {}: expected CALL <name>", lineno);
            return 1;
        }

        // handle dotted names for function calls
        // like:
        // CALL MyModule.MyFunc()
        while (*skip_ws(p) == '.') {
            p = skip_ws(p) + 1;
            std::string part;
            if (!parse_identifier(p, &p, part)) {
                std::println(stderr, "Syntax error line {}: expected identifier after '.' in CALL",
                             lineno);
                return 1;
            }
            name += "__" + part;
        }

        p = skip_ws(p);

        if (*p == '(') {
            const FuncDef* func_def = nullptr;
            if (funcs) {
                for (auto& f : *funcs) {
                    if (f.name == name) {
                        func_def = &f;
                        break;
                    }
                }
            }

            p = skip_ws(p + 1);
            int arg_index = 0;
            while (*p && *p != ')') {
                if (func_def && arg_index < static_cast<int>(func_def->param_is_ref.size()) &&
                    func_def->param_is_ref[arg_index]) {
                    // ref param
                    std::string refname;
                    const char* ref_end = p;
                    if (!parse_identifier(p, &ref_end, refname)) {
                        std::println(stderr, "Error line {}: ref param requires a variable name",
                                     lineno);
                        return 1;
                    }
                    Variable* rv = sym_find(refname.data());
                    if (!rv) {
                        std::println(stderr, "Undefined variable '{}' on line {}", refname, lineno);
                        return 1;
                    }
                    p = ref_end;
                    RegGuard rg(&ra);
                    if (!rg.ok()) {
                        std::println(stderr, "Out of scratch registers");
                        return 1;
                    }
                    char rn[4];
                    reg_name(rg, rn);
                    EMIT_CODE(this, "    MOVI %s, %u ; ref slot for %s", rn, rv->slot,
                              refname.data());
                    EMIT_CODE(this, "    PUSH %s ; ref arg for call to %s", rn, name.data());
                } else {
                    // value param
                    p = skip_ws(p);
                    // if the string parameter is passed directly
                    if (*p == '"') {
                        const char* str_end = strchr(p + 1, '"');
                        if (!str_end) {
                            std::println(stderr,
                                         "Syntax error line {}: unterminated string in CALL '{}'",
                                         lineno, name);
                            return 1;
                        }
                        char data_name[64];
                        snprintf(data_name, sizeof(data_name), "_p%lu", uid++);
                        EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name,
                                  static_cast<int>(str_end - p - 1), p + 1);
                        RegGuard rg(&ra);
                        if (!rg.ok()) {
                            std::println(stderr, "Out of scratch registers");
                            return 1;
                        }
                        char rn[4];
                        reg_name(rg, rn);
                        EMIT_CODE(this, "    LOADSTR $%s, %s", data_name, rn);
                        EMIT_CODE(this, "    PUSH %s ; string argument for call to %s", rn,
                                  name.data());
                        p = str_end + 1;
                    } else {
                        int areg;
                        if (emit_expr_p(p, &p, &areg)) {
                            return 1;
                        }
                        char arn[4];
                        reg_name(areg, arn);
                        EMIT_CODE(this, "    PUSH %s ; argument for call to %s", arn, name.data());
                        ralloc_release(areg);
                    }
                }
                arg_index++;
                p = skip_ws(p);
                if (*p == ',') {
                    p = skip_ws(p + 1);
                    continue;
                }
                if (*p == ')') {
                    break;
                }
                std::println(stderr, "Syntax error line {}: expected ',' or ')' in CALL '{}'",
                             lineno, name);
                return 1;
            }
            if (*p != ')') {
                std::println(stderr, "Syntax error line {}: missing ')' in CALL '{}'", lineno,
                             name);
                return 1;
            }
            p = skip_ws(p + 1);
            if (*p != '\0') {
                std::println(stderr, "Syntax error line {}: unexpected tokens after CALL", lineno);
                return 1;
            }
            EMIT_CODE(this, "    CALL __bbx_func_%s", name.data());
            if (debug) {
                std::println("[BASIC] CALL {}(...)", name);
            }
            return 0;
        }

        // plain CALL
        if (*p != '\0') {
            std::println(stderr, "Syntax error line {}: unexpected tokens after CALL target",
                         lineno);
            return 1;
        }
        EMIT_CODE(this, "    CALL %s", name.data());
        if (debug) {
            std::println("[BASIC] CALL {}", name);
        }
        return 0;
    }

    // RETURN
    if (blackbox::tools::starts_with_ci(s, "RETURN")) {
        const char* arg = skip_ws(s + 6);
        if (*arg != '\0') {
            // RETURN expr: evaluate into a reg, move to R00
            if (!in_func) {
                std::println(stderr, "Error line {}: RETURN with value outside FUNC", lineno);
                return 1;
            }
            const char* expr_end = nullptr;
            int reg;
            if (emit_expr_p(arg, &expr_end, &reg)) {
                return 1;
            }
            if (*skip_ws(expr_end) != '\0') {
                ralloc_release(reg);
                std::println(stderr,
                             "Syntax error line {}: unexpected tokens after RETURN expression",
                             lineno);
                return 1;
            }
            char rn[4];
            reg_name(reg, rn);
            EMIT_CODE(this, "    MOV R00, %s", rn);
            ralloc_release(reg);
        }
        EMIT_CODE(this, "    RET ;", *arg ? " with value" : "");
        if (debug) {
            std::println("[BASIC] RETURN{}", *arg ? " <expr>" : "");
        }
        return 0;
    }

    // EXEC
    if (blackbox::tools::starts_with_ci(s, "EXEC")) {
        const char* arg = s + 4;
        if (*arg != '\0' && !isspace(static_cast<unsigned char>(*arg))) {
            std::println(stderr, "Syntax error line {}: expected EXEC \"<cmd>\", <var>", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg != '"') {
            std::println(stderr, "Syntax error line {}: expected EXEC \"<cmd>\", <var>", lineno);
            return 1;
        }
        const char* str_start = arg + 1;
        const char* str_end = strchr(str_start, '"');
        if (!str_end) {
            std::println(stderr, "Syntax error line {}: missing closing quote for EXEC", lineno);
            return 1;
        }
        std::string cmd(str_start, static_cast<size_t>(str_end - str_start));
        const char* after = skip_ws(str_end + 1);
        int have_dest = 0;
        Variable* dv = nullptr;
        if (*after != '\0') {
            if (*after != ',') {
                std::println(stderr, "Syntax error line {}: expected ',' before EXEC destination",
                             lineno);
                return 1;
            }
            after = skip_ws(after + 1);
            std::string varname;
            const char* var_end = after;
            if (!parse_identifier(after, &var_end, varname)) {
                std::println(stderr, "Syntax error line {}: expected EXEC destination variable",
                             lineno);
                return 1;
            }
            after = skip_ws(var_end);
            if (*after != '\0') {
                std::println(stderr,
                             "Syntax error line {}: unexpected tokens after EXEC destination",
                             lineno);
                return 1;
            }
            dv = sym_find(varname.data());
            if (!dv) {
                std::println(stderr, "Undefined variable '{}' on line {}", varname, lineno);
                return 1;
            }
            if (dv->is_const) {
                std::println(stderr, "Error line {}: cannot assign to CONST '{}'", lineno, varname);
                return 1;
            }
            if (dv->type != VAR_INT) {
                std::println(stderr, "Type error line {}: EXEC destination '{}' must be integer",
                             lineno, varname);
                return 1;
            }
            have_dest = 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    EXEC \"%s\", %s", cmd.data(), rn);
        if (have_dest) {
            EMIT_CODE_META(this, dv->name, "    STOREVAR %s, %u", rn, dv->slot);
        }
        if (debug) {
            if (have_dest) {
                std::println("[BASIC] EXEC \"{}\" -> {}", cmd, dv->name);
            } else {
                std::println("[BASIC] EXEC \"{}\"", cmd);
            }
        }
        return 0;
    }

    // FOPEN
    if (blackbox::tools::starts_with_ci(s, "FOPEN")) {
        std::string arg = trim_copy(s + 5);
        size_t pos = 0;
        std::string mode;
        if (!parse_file_mode(arg, pos, mode)) {
            std::println(stderr,
                         "Syntax error line {}: expected FOPEN <mode>, <handle>, \"<file>\"",
                         lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            std::println(stderr,
                         "Syntax error line {}: expected FOPEN <mode>, <handle>, \"<file>\"",
                         lineno);
            return 1;
        }
        pos = skip_ws(arg, pos + 1);
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            std::println(stderr,
                         "Syntax error line {}: expected FOPEN <mode>, <handle>, \"<file>\"",
                         lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            std::println(stderr,
                         "Syntax error line {}: expected FOPEN <mode>, <handle>, \"<file>\"",
                         lineno);
            return 1;
        }
        pos = skip_ws(arg, pos + 1);
        std::string filename;
        if (!parse_quoted_string(arg, pos, filename)) {
            std::println(stderr,
                         "Syntax error line {}: expected FOPEN <mode>, <handle>, \"<file>\"",
                         lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos != arg.size()) {
            std::println(stderr, "Syntax error line {}: unexpected trailing tokens in FOPEN",
                         lineno);
            return 1;
        }
        Variable* v = sym_find(handle_name.data());
        if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", handle_name, lineno);
            return 1;
        }
        if (v->is_const) {
            std::println(stderr, "Error line {}: cannot use CONST '{}' as file handle", lineno,
                         handle_name);
            return 1;
        }
        if (v->type != VAR_INT) {
            std::println(stderr, "Type error line {}: file handle '{}' must be integer", lineno,
                         handle_name);
            return 1;
        }
        uint8_t fd;
        if (alloc_file_handle_fd(handle_name.data(), &fd)) {
            return 1;
        }
        EMIT_CODE(this, "    FOPEN %s, F%u, \"%s\"", mode.data(), fd, filename.data());
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    MOVI %s, %u", rn, fd);
        EMIT_CODE_META(this, v->name, "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            std::println("[BASIC] FOPEN {} -> {}", filename, handle_name);
        }
        return 0;
    }

    // FCLOSE
    if (blackbox::tools::starts_with_ci(s, "FCLOSE")) {
        std::string arg = trim_copy(s + 6);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            std::println(stderr, "Syntax error line {}: expected FCLOSE <handle>", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos != arg.size()) {
            std::println(stderr, "Syntax error line {}: FCLOSE takes exactly one operand", lineno);
            return 1;
        }
        uint8_t fd;
        if (get_file_handle_fd(handle_name.data(), &fd)) {
            return 1;
        }
        EMIT_CODE(this, "    FCLOSE F%u", fd);
        if (debug) {
            std::println("[BASIC] FCLOSE {}", handle_name);
        }
        return 0;
    }

    // FREAD
    if (blackbox::tools::starts_with_ci(s, "FREAD")) {
        std::string arg = trim_copy(s + 5);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            std::println(stderr, "Syntax error line {}: expected FREAD <handle>, <variable>",
                         lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            std::println(stderr, "Syntax error line {}: expected FREAD <handle>, <variable>",
                         lineno);
            return 1;
        }
        pos = skip_ws(arg, pos + 1);
        std::string target_name;
        if (!parse_identifier(arg, pos, target_name)) {
            std::println(stderr, "Syntax error line {}: expected FREAD <handle>, <variable>",
                         lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos != arg.size()) {
            std::println(stderr, "Syntax error line {}: FREAD takes exactly two operands", lineno);
            return 1;
        }
        Variable* dest = sym_find(target_name.data());
        if (!dest) {
            std::println(stderr, "Undefined variable '{}' on line {}", target_name, lineno);
            return 1;
        }
        if (dest->is_const) {
            std::println(stderr, "Error line {}: cannot assign to CONST '{}'", lineno, target_name);
            return 1;
        }
        if (dest->type != VAR_INT) {
            std::println(stderr, "Type error line {}: FREAD target '{}' must be integer", lineno,
                         target_name);
            return 1;
        }
        uint8_t fd;
        if (get_file_handle_fd(handle_name.data(), &fd)) {
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    FREAD F%u, %s", fd, rn);
        EMIT_CODE_META(this, dest->name, "    STOREVAR %s, %u", rn, dest->slot);
        if (debug) {
            std::println("[BASIC] FREAD {} -> {}", handle_name, target_name);
        }
        return 0;
    }

    // FWRITE
    if (blackbox::tools::starts_with_ci(s, "FWRITE")) {
        std::string arg = trim_copy(s + 6);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            std::println(stderr, "Syntax error line {}: expected FWRITE <handle>, <expr>", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            std::println(stderr, "Syntax error line {}: expected FWRITE <handle>, <expr>", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos + 1);
        uint8_t fd;
        if (get_file_handle_fd(handle_name.data(), &fd)) {
            return 1;
        }
        const char* expr = arg.c_str() + pos;
        const char* expr_end = nullptr;
        int reg;
        if (emit_expr_p(expr, &expr_end, &reg)) {
            return 1;
        }
        if (*skip_ws(expr_end) != '\0') {
            ralloc_release(reg);
            std::println(stderr, "Syntax error line {}: FWRITE takes a single expression", lineno);
            return 1;
        }
        char rn[4];
        reg_name(reg, rn);
        EMIT_CODE(this, "    FWRITE F%u, %s", fd, rn);
        ralloc_release(reg);
        if (debug) {
            std::println("[BASIC] FWRITE {}", handle_name);
        }
        return 0;
    }

    // FSEEK
    if (blackbox::tools::starts_with_ci(s, "FSEEK")) {
        std::string arg = trim_copy(s + 5);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            std::println(stderr, "Syntax error line {}: expected FSEEK <handle>, <expr>", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            std::println(stderr, "Syntax error line {}: expected FSEEK <handle>, <expr>", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos + 1);
        uint8_t fd;
        if (get_file_handle_fd(handle_name.data(), &fd)) {
            return 1;
        }
        const char* expr = arg.c_str() + pos;
        const char* expr_end = nullptr;
        int reg;
        if (emit_expr_p(expr, &expr_end, &reg)) {
            return 1;
        }
        if (*skip_ws(expr_end) != '\0') {
            ralloc_release(reg);
            std::println(stderr, "Syntax error line {}: FSEEK takes a single expression", lineno);
            return 1;
        }
        char rn[4];
        reg_name(reg, rn);
        EMIT_CODE(this, "    FSEEK F%u, %s", fd, rn);
        ralloc_release(reg);
        if (debug) {
            std::println("[BASIC] FSEEK {}", handle_name);
        }
        return 0;
    }

    // FPRINT
    if (blackbox::tools::starts_with_ci(s, "FPRINT")) {
        std::string arg = trim_copy(s + 6);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            std::println(stderr, "Syntax error line {}: expected FPRINT <handle>, <value>", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            std::println(stderr, "Syntax error line {}: expected FPRINT <handle>, <value>", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos + 1);
        uint8_t fd;
        if (get_file_handle_fd(handle_name.data(), &fd)) {
            return 1;
        }
        if (pos < arg.size() && arg[pos] == '"') {
            std::string text;
            if (!parse_quoted_string(arg, pos, text)) {
                std::println(stderr, "Syntax error line {}: unterminated string in FPRINT", lineno);
                return 1;
            }
            pos = skip_ws(arg, pos);
            if (pos != arg.size()) {
                std::println(stderr, "Syntax error line {}: unexpected tokens after FPRINT string",
                             lineno);
                return 1;
            }
            RegGuard rg(&ra);
            if (!rg.ok()) {
                std::println(stderr, "Out of scratch registers");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            for (unsigned char c : text) {
                EMIT_CODE(this, "    MOVI %s, %u", rn, static_cast<unsigned>(c));
                EMIT_CODE(this, "    FWRITE F%u, %s", fd, rn);
            }
            EMIT_CODE(this, "    MOVI %s, 10", rn);
            EMIT_CODE(this, "    FWRITE F%u, %s", fd, rn);
        } else {
            const char* expr = arg.c_str() + pos;
            const char* expr_end = nullptr;
            int reg;
            if (emit_expr_p(expr, &expr_end, &reg)) {
                return 1;
            }
            if (*skip_ws(expr_end) != '\0') {
                ralloc_release(reg);
                std::println(stderr, "Syntax error line {}: FPRINT takes a single expression",
                             lineno);
                return 1;
            }
            char rn[4];
            reg_name(reg, rn);
            EMIT_CODE(this, "    FWRITE F%u, %s", fd, rn);
            EMIT_CODE(this, "    MOVI %s, 10", rn);
            EMIT_CODE(this, "    FWRITE F%u, %s", fd, rn);
            ralloc_release(reg);
        }
        if (debug) {
            std::println("[BASIC] FPRINT {}", handle_name);
        }
        return 0;
    }

    // INPUT
    if (blackbox::tools::starts_with_ci(s, "INPUT ")) {
        const char* name = skip_ws(s + 6);
        if (*name == '"') {
            const char* str_end = strchr(name + 1, '"');
            if (!str_end) {
                std::println(stderr, "Syntax error line {}: unterminated string in INPUT", lineno);
                return 1;
            }
            std::string prompt(name, static_cast<size_t>(str_end - name + 1));
            if (emit_write_values(prompt.data(), "WRITE", 0)) {
                return 1;
            }
            name = skip_ws(str_end + 1);
            if (*name != ',') {
                std::println(stderr, "Syntax error line {}: expected ',' after INPUT prompt",
                             lineno);
                return 1;
            }
            name = skip_ws(name + 1);
        }
        Variable* v = sym_find(name);
        if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", name, lineno);
            return 1;
        }
        RegGuard rg(&ra);
        char rn[4];
        reg_name(rg, rn);
        if (v->type == VAR_STR) {
            EMIT_CODE(this, "    READSTR %s", rn);
            EMIT_CODE_META(this, name, "    STOREVAR %s, %u", rn, v->slot);
        } else {
            EMIT_CODE(this, "    READ %s", rn);
            EMIT_CODE_META(this, name, "    STOREVAR %s, %u", rn, v->slot);
        }
        if (debug) {
            std::println("[BASIC] INPUT {}", name);
        }
        return 0;
    }

    // GETKEY
    if (blackbox::tools::starts_with_ci(s, "GETKEY")) {
        const char* p = skip_ws(s + 6);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            std::println(stderr, "Syntax error line {}: expected GETKEY <identifier>", lineno);
            return 1;
        }
        if (*skip_ws(p) != '\0') {
            std::println(stderr, "Syntax error line {}: GETKEY takes at most one operand", lineno);
            return 1;
        }
        Variable* v = sym_find(name.data());
        if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", name, lineno);
            return 1;
        }
        if (v->is_const) {
            std::println(stderr, "Error line {}: cannot assign to CONST '{}'", lineno, name);
            return 1;
        }
        if (v->type != VAR_INT) {
            std::println(stderr, "Type error line {}: GETKEY target must be integer", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    GETKEY %s", rn);
        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            std::println("[BASIC] GETKEY {}", name);
        }
        return 0;
    }

    // GETARGC
    if (blackbox::tools::starts_with_ci(s, "GETARGC")) {
        const char* p = skip_ws(s + 7);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            std::println(stderr, "Syntax error line {}: expected GETARGC <identifier>", lineno);
            return 1;
        }
        if (*skip_ws(p) != '\0') {
            std::println(stderr, "Syntax error line {}: GETARGC takes at most one operand", lineno);
            return 1;
        }
        Variable* v = sym_find(name.data());
        if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", name, lineno);
            return 1;
        }
        if (v->is_const) {
            std::println(stderr, "Error line {}: cannot assign to CONST '{}'", lineno, name);
            return 1;
        }
        if (v->type != VAR_INT) {
            std::println(stderr, "Type error line {}: GETARGC target must be integer", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    GETARGC %s", rn);
        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            std::println("[BASIC] GETARGC {}", name);
        }
        return 0;
    }

    // GETARG
    if (blackbox::tools::starts_with_ci(s, "GETARG")) {
        const char* p = skip_ws(s + 6);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            std::println(stderr, "Syntax error line {}: expected GETARG <identifier>, <index>",
                         lineno);
            return 1;
        }
        p = skip_ws(p);
        if (*p != ',') {
            std::println(stderr, "Syntax error line {}: expected GETARG <identifier>, <index>",
                         lineno);
            return 1;
        }
        p = skip_ws(p + 1);
        std::string idx_tok;
        if (!parse_identifier(p, &p, idx_tok)) {
            std::println(stderr, "Syntax error line {}: expected GETARG <identifier>, <index>",
                         lineno);
            return 1;
        }
        if (*skip_ws(p) != '\0') {
            std::println(stderr, "Syntax error line {}: GETARG takes exactly two operands", lineno);
            return 1;
        }
        char* endptr;
        unsigned long idx = strtoul(idx_tok.data(), &endptr, 0);
        if (*endptr != '\0') {
            std::println(stderr, "Syntax error line {}: invalid GETARG index '{}'", lineno,
                         idx_tok);
            return 1;
        }
        Variable* v = sym_find(name.data());
        if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", name, lineno);
            return 1;
        }
        if (v->is_const) {
            std::println(stderr, "Error line {}: cannot assign to CONST '{}'", lineno, name);
            return 1;
        }
        if (v->type != VAR_STR) {
            std::println(stderr, "Type error line {}: GETARG target must be string", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    GETARG %s, %lu", rn, idx);
        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            std::println("[BASIC] GETARG {}, {}", name, idx);
        }
        return 0;
    }

    // GETENV
    if (blackbox::tools::starts_with_ci(s, "GETENV")) {
        const char* p = skip_ws(s + 6);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            std::println(stderr, "Syntax error line {}: expected GETENV <identifier>, <varname>",
                         lineno);
            return 1;
        }
        p = skip_ws(p);
        if (*p != ',') {
            std::println(stderr, "Syntax error line {}: expected GETENV <identifier>, <varname>",
                         lineno);
            return 1;
        }
        p = skip_ws(p + 1);
        std::string envname;
        if (*p == '"') {
            const char* end = strchr(p + 1, '"');
            if (!end) {
                std::println(stderr, "Syntax error line {}: unterminated string in GETENV", lineno);
                return 1;
            }
            envname.assign(p + 1, static_cast<size_t>(end - (p + 1)));
            p = skip_ws(end + 1);
        } else {
            const char* next = p;
            if (!parse_identifier(p, &next, envname)) {
                std::println(stderr,
                             "Syntax error line {}: expected GETENV <identifier>, <varname>",
                             lineno);
                return 1;
            }
            p = skip_ws(next);
        }
        if (*p != '\0') {
            std::println(stderr, "Syntax error line {}: GETENV takes exactly two operands", lineno);
            return 1;
        }
        Variable* v = sym_find(name.data());
        if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", name, lineno);
            return 1;
        }
        if (v->is_const) {
            std::println(stderr, "Error line {}: cannot assign to CONST '{}'", lineno, name);
            return 1;
        }
        if (v->type != VAR_STR) {
            std::println(stderr, "Type error line {}: GETENV target must be string", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    GETENV %s, \"%s\"", rn, envname.data());
        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            std::println("[BASIC] GETENV {}, {}", name, envname);
        }
        return 0;
    }

    // RANDOM
    if (blackbox::tools::starts_with_ci(s, "RANDOM")) {
        const char* p = skip_ws(s + 6);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            std::println(stderr, "Syntax error line {}: expected RANDOM <identifier>", lineno);
            return 1;
        }
        p = skip_ws(p);
        Variable* v = sym_find(name.data());
        if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", name, lineno);
            return 1;
        }
        if (v->is_const) {
            std::println(stderr, "Error line {}: cannot assign to CONST '{}'", lineno, name);
            return 1;
        }
        if (v->type != VAR_INT) {
            std::println(stderr, "Type error line {}: RANDOM target must be integer", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        if (*p == ',') {
            p = skip_ws(p + 1);
            std::string range_text(p);
            size_t comma_pos = range_text.find(',');
            if (comma_pos == std::string::npos) {
                std::println(stderr, "Syntax error line {}: expected RANDOM <n>, <min>, <max>",
                             lineno);
                return 1;
            }
            std::string min_str = trim_copy(range_text.substr(0, comma_pos));
            std::string max_str = trim_copy(range_text.substr(comma_pos + 1));
            if (min_str.empty() || max_str.empty()) {
                std::println(stderr, "Syntax error line {}: expected RANDOM <n>, <min>, <max>",
                             lineno);
                return 1;
            }
            EMIT_CODE_META(this, name.data(), "    RAND %s, %s, %s", rn, min_str.data(),
                           max_str.data());
        } else {
            EMIT_CODE_META(this, name.data(), "    RAND %s", rn);
        }
        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            std::println("[BASIC] RANDOM {}", name);
        }
        return 0;
    }

    // FOR
    if (blackbox::tools::starts_with_ci(s, "FOR ")) {
        size_t slen = strlen(s);
        if (slen > 0 && s[slen - 1] == ':') {
            s[slen - 1] = '\0';
        }

        const char* p = skip_ws(s + 4);
        int inline_decl = 0;
        if (blackbox::tools::starts_with_ci(p, "VAR") &&
            !(isalnum(static_cast<unsigned char>(p[3])) || p[3] == '_')) {
            inline_decl = 1;
            p = skip_ws(p + 3);
        }

        std::string var_name;
        if (!parse_identifier(p, &p, var_name)) {
            std::println(stderr, "Syntax error line {}: expected FOR [VAR] <identifier> = ...",
                         lineno);
            return 1;
        }
        p = skip_ws(p);
        if (*p != '=') {
            std::println(stderr, "Syntax error line {}: expected '=' in FOR statement", lineno);
            return 1;
        }
        const char* init_start = skip_ws(p + 1);
        const char* to_kw = find_keyword_token(init_start, "TO");
        if (!to_kw) {
            std::println(stderr, "Syntax error line {}: expected TO in FOR statement", lineno);
            return 1;
        }
        const char* to_rhs = skip_ws(to_kw + 2);
        const char* step_kw = find_keyword_token(to_rhs, "STEP");

        std::string init_expr =
            trim_copy(std::string(init_start, static_cast<size_t>(to_kw - init_start)));
        std::string limit_expr = trim_copy(std::string(
            to_rhs, static_cast<size_t>((step_kw ? step_kw : to_rhs + strlen(to_rhs)) - to_rhs)));
        std::string step_expr =
            step_kw
                ? trim_copy(std::string(
                      step_kw + 4, static_cast<size_t>((to_rhs + strlen(to_rhs)) - (step_kw + 4))))
                : std::string("1");

        if (init_expr.empty() || limit_expr.empty() || step_expr.empty()) {
            std::println(stderr, "Syntax error line {}: FOR requires init, TO, and STEP", lineno);
            return 1;
        }

        Variable* v = sym_find(var_name.data());
        if (inline_decl) {
            if (v) {
                std::println(stderr, "Error line {}: variable '{}' already defined", lineno,
                             var_name);
                return 1;
            }
            v = sym_add_int(var_name.data());
        } else if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", var_name, lineno);
            return 1;
        }
        if (v->is_const) {
            std::println(stderr, "Error line {}: cannot use CONST '{}' as FOR variable", lineno,
                         var_name);
            return 1;
        }
        if (v->type != VAR_INT) {
            std::println(stderr, "Type error line {}: FOR control variable '{}' must be integer",
                         lineno, var_name);
            return 1;
        }

        uint32_t limit_slot = symbol_table.next_slot++;
        uint32_t step_slot = symbol_table.next_slot++;

        {
            int ireg;
            if (emit_expr(init_expr.data(), &ireg)) {
                return 1;
            }
            char irn[4];
            reg_name(ireg, irn);
            EMIT_CODE_META(this, var_name.data(), "    STOREVAR %s, %u", irn, v->slot);
            ralloc_release(ireg);
        }
        {
            int lreg;
            if (emit_expr(limit_expr.data(), &lreg)) {
                return 1;
            }
            char lrn[4];
            reg_name(lreg, lrn);
            EMIT_CODE_META(this, "for-limit", "    STOREVAR %s, %u", lrn, limit_slot);
            ralloc_release(lreg);
        }
        {
            int sreg;
            if (emit_expr(step_expr.data(), &sreg)) {
                return 1;
            }
            char srn[4];
            reg_name(sreg, srn);
            EMIT_CODE_META(this, "for-step", "    STOREVAR %s, %u", srn, step_slot);
            ralloc_release(sreg);
        }

        char loop_label[64], end_label[64], neg_check_label[64], body_label[64];
        snprintf(loop_label, sizeof(loop_label), "for_%lu", uid);
        snprintf(end_label, sizeof(end_label), "endfor_%lu", uid);
        snprintf(neg_check_label, sizeof(neg_check_label), "for_neg_%lu", uid);
        snprintf(body_label, sizeof(body_label), "for_body_%lu", uid);
        uid++;

        Block b = {};
        b.kind = BLOCK_FOR;
        b.for_var_slot = v->slot;
        b.for_limit_slot = limit_slot;
        b.for_step_slot = step_slot;
        copy_cstr(b.for_var_name, sizeof(b.for_var_name), var_name.data());
        snprintf(b.loop_label, sizeof(b.loop_label), ".%s", loop_label);
        snprintf(b.end_label, sizeof(b.end_label), ".%s", end_label);

        EMIT_CODE(this, "%s:", b.loop_label);

        RegGuard step_r(&ra), zero_r(&ra), var_r(&ra), limit_r(&ra);
        if (!step_r.ok() || !zero_r.ok() || !var_r.ok() || !limit_r.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char step_rn[4], zero_rn[4], var_rn[4], limit_rn[4];
        reg_name(step_r, step_rn);
        reg_name(zero_r, zero_rn);
        reg_name(var_r, var_rn);
        reg_name(limit_r, limit_rn);

        EMIT_CODE_META(this, "for-step", "    LOADVAR %s, %u", step_rn, step_slot);
        EMIT_CODE(this, "    MOVI %s, 0", zero_rn);
        EMIT_CODE(this, "    CMP %s, %s", step_rn, zero_rn);
        EMIT_CODE(this, "    JL %s", neg_check_label);
        EMIT_CODE_META(this, var_name.data(), "    LOADVAR %s, %u", var_rn, v->slot);
        EMIT_CODE_META(this, "for-limit", "    LOADVAR %s, %u", limit_rn, limit_slot);
        EMIT_CODE(this, "    CMP %s, %s", limit_rn, var_rn);
        EMIT_CODE(this, "    JL %s", b.end_label + 1);
        EMIT_CODE(this, "    JMP %s", body_label);
        EMIT_CODE(this, ".%s:", neg_check_label);
        EMIT_CODE_META(this, var_name.data(), "    LOADVAR %s, %u", var_rn, v->slot);
        EMIT_CODE_META(this, "for-limit", "    LOADVAR %s, %u", limit_rn, limit_slot);
        EMIT_CODE(this, "    CMP %s, %s", var_rn, limit_rn);
        EMIT_CODE(this, "    JL %s", b.end_label + 1);
        EMIT_CODE(this, ".%s:", body_label);

        bstack_push(b);
        if (debug) {
            std::println("[BASIC] FOR {} = ({}) TO ({}) STEP ({})", var_name, init_expr, limit_expr,
                         step_expr);
        }
        return 0;
    }

    // NEXT
    if (blackbox::tools::starts_with_ci(s, "NEXT")) {
        const char* arg = skip_ws(s + 4);
        Block* top = bstack_peek();
        if (!top || top->kind != BLOCK_FOR) {
            std::println(stderr, "Error line {}: NEXT without FOR", lineno);
            return 1;
        }
        if (*arg) {
            std::string next_var;
            const char* next_end = arg;
            if (!parse_identifier(arg, &next_end, next_var)) {
                std::println(stderr, "Syntax error line {}: expected NEXT [<identifier>]", lineno);
                return 1;
            }
            if (*skip_ws(next_end) != '\0') {
                std::println(stderr, "Syntax error line {}: expected NEXT [<identifier>]", lineno);
                return 1;
            }
            if (!blackbox::tools::equals_ci(next_var.data(), top->for_var_name)) {
                std::println(stderr,
                             "Error line {}: NEXT variable '{}' does not match FOR variable '{}'",
                             lineno, next_var, top->for_var_name);
                return 1;
            }
        }
        Block b = bstack_pop();
        RegGuard var_r(&ra), step_r(&ra);
        if (!var_r.ok() || !step_r.ok()) {
            std::println(stderr, "Out of scratch registers");
            return 1;
        }
        char vrn[4], srn[4];
        reg_name(var_r, vrn);
        reg_name(step_r, srn);
        EMIT_CODE_META(this, b.for_var_name, "    LOADVAR %s, %u", vrn, b.for_var_slot);
        EMIT_CODE_META(this, "for-step", "    LOADVAR %s, %u", srn, b.for_step_slot);
        EMIT_CODE(this, "    ADD %s, %s", vrn, srn);
        EMIT_CODE_META(this, b.for_var_name, "    STOREVAR %s, %u", vrn, b.for_var_slot);
        EMIT_CODE(this, "    JMP %s", b.loop_label + 1);
        EMIT_CODE(this, "%s:", b.end_label);
        if (debug) {
            std::println("[BASIC] NEXT {}", b.for_var_name);
        }
        return 0;
    }

    // INC
    if (blackbox::tools::starts_with_ci(s, "INC")) {
        const char* name = skip_ws(s + 4);
        Variable* v = sym_find(name);
        if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", name, lineno);
            return 1;
        }
        if (v->is_const) {
            std::println(stderr, "Error line {}: cannot INC CONST '{}'", lineno, name);
            return 1;
        }
        if (v->type != VAR_INT) {
            std::println(stderr, "Type error line {}: cannot INC non-integer '{}'", lineno, name);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers on line {}", lineno);
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE_META(this, name, "    LOADVAR %s, %u", rn, v->slot);
        EMIT_CODE(this, "    INC %s", rn);
        EMIT_CODE_META(this, name, "    STOREVAR %s, %u", rn, v->slot);
        return 0;
    }

    // DEC
    if (blackbox::tools::starts_with_ci(s, "DEC")) {
        const char* name = skip_ws(s + 4);
        Variable* v = sym_find(name);
        if (!v) {
            std::println(stderr, "Undefined variable '{}' on line {}", name, lineno);
            return 1;
        }
        if (v->is_const) {
            std::println(stderr, "Error line {}: cannot DEC CONST '{}'", lineno, name);
            return 1;
        }
        if (v->type != VAR_INT) {
            std::println(stderr, "Type error line {}: cannot DEC non-integer '{}'", lineno, name);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            std::println(stderr, "Out of scratch registers on line {}", lineno);
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE_META(this, name, "    LOADVAR %s, %u", rn, v->slot);
        EMIT_CODE(this, "    DEC %s", rn);
        EMIT_CODE_META(this, name, "    STOREVAR %s, %u", rn, v->slot);
        return 0;
    }

    // BREAK
    if (blackbox::tools::equals_ci(s, "BREAK")) {
        Block* target = nullptr;
        for (int i = static_cast<int>(block_stack.items.size()) - 1; i >= 0; i--) {
            if (block_stack.items[i].kind == BLOCK_WHILE ||
                block_stack.items[i].kind == BLOCK_FOR) {
                target = &block_stack.items[i];
                break;
            }
        }
        if (!target) {
            std::println(stderr, "Error line {}: BREAK outside WHILE or FOR loop", lineno);
            return 1;
        }
        EMIT_CODE(this, "    JMP %s", target->end_label + 1);
        if (debug) {
            std::println("[BASIC] BREAK");
        }
        return 0;
    }

    // CONTINUE
    if (blackbox::tools::equals_ci(s, "CONTINUE")) {
        Block* target = nullptr;
        for (int i = static_cast<int>(block_stack.items.size()) - 1; i >= 0; i--) {
            if (block_stack.items[i].kind == BLOCK_WHILE ||
                block_stack.items[i].kind == BLOCK_FOR) {
                target = &block_stack.items[i];
                break;
            }
        }
        if (!target) {
            std::println(stderr, "Error line {}: CONTINUE outside WHILE or FOR loop", lineno);
            return 1;
        }
        if (target->kind == BLOCK_WHILE) {
            EMIT_CODE(this, "    JMP %s", target->loop_label + 1);
            if (debug) {
                std::println("[BASIC] CONTINUE (WHILE)");
            }
        } else {
            RegGuard var_r(&ra), step_r(&ra);
            if (!var_r.ok() || !step_r.ok()) {
                std::println(stderr, "Out of scratch registers");
                return 1;
            }
            char vrn[4], srn[4];
            reg_name(var_r, vrn);
            reg_name(step_r, srn);
            EMIT_CODE_META(this, target->for_var_name, "    LOADVAR %s, %u", vrn,
                           target->for_var_slot);
            EMIT_CODE_META(this, "for-step", "    LOADVAR %s, %u", srn, target->for_step_slot);
            EMIT_CODE(this, "    ADD %s, %s", vrn, srn);
            EMIT_CODE_META(this, target->for_var_name, "    STOREVAR %s, %u", vrn,
                           target->for_var_slot);
            EMIT_CODE(this, "    JMP %s", target->loop_label + 1);
            if (debug) {
                std::println("[BASIC] CONTINUE (FOR {})", target->for_var_name);
            }
        }
        return 0;
    }

    // CLRSCR
    if (blackbox::tools::equals_ci(s, "CLRSCR")) {
        EMIT_CODE(this, "    CLRSCR");
        if (debug) {
            std::println("[BASIC] CLRSCR");
        }
        return 0;
    }

    std::println(stderr, "Unknown statement on line {}: {}", lineno, s);
    return 1;
}

int preprocess_basic(const char* input_file, const char* output_file, int debug) {
    FILE* in = fopen(input_file, "r");
    if (!in) {
        perror("fopen input");
        return 1;
    }

    CompilerState compiler_state;
    compiler_state.debug = debug;

    std::vector<FuncDef> funcs;
    compiler_state.funcs = &funcs;
    FuncDef* current_func = nullptr;

    std::vector<NamespaceDef> namespaces;
    NamespaceDef* current_namespace = nullptr;

    bool in_asm_block = false;
    int result = 0;
    char line[8192];

    while (fgets(line, sizeof(line), in)) {
        compiler_state.lineno++;
        std::string stmt = trim_copy(line);
        size_t comment_pos = stmt.find("//");
        if (comment_pos != std::string::npos) {
            stmt.erase(comment_pos);
            stmt = trim_copy(stmt);
        }
        if (stmt.empty()) {
            continue;
        }

        char* s = &stmt[0];
        if (blackbox::tools::starts_with_ci(s, "NAMESPACE ")) {
            if (current_func || current_namespace) {
                std::println(stderr, "Error line {}: NAMESPACE cannot be nested",
                             compiler_state.lineno);
                result = 1;
                break;
            }

            std::string namespace_name = trim_copy(std::string(s + 10));
            if (namespace_name.empty()) {
                std::println(stderr, "Syntax error on line {}: expected NAMESPACE <name>",
                             compiler_state.lineno);
                std::println(stderr, "Got: {}", line);
                result = 1;
                break;
            }

            for (auto& ns : namespaces) {
                if (ns.name == namespace_name) {
                    std::println(stderr, "Error line {}: namespace '{}' already defined",
                                 compiler_state.lineno, namespace_name);
                    result = 1;
                    break;
                }
            }

            if (result) {
                break;
            }

            namespaces.emplace_back();
            current_namespace = &namespaces.back();
            current_namespace->name = namespace_name;
            current_namespace->state.debug = debug;
            current_namespace->state.uid = compiler_state.uid;
            current_namespace->state.funcs = &current_namespace->funcs;
            current_namespace->state.current_namespace = namespace_name;
            current_namespace->state.symbol_table.next_slot = compiler_state.symbol_table.next_slot;

            if (debug) {
                std::println("[BASIC] NAMESPACE {}", namespace_name);
            }
            continue;
        }
        if (blackbox::tools::equals_ci(s, "ENDNAMESPACE")) {
            if (!current_namespace) {
                std::println(stderr, "Error line {}: ENDNAMESPACE without NAMESPACE",
                             compiler_state.lineno);
                result = 1;
                break;
            }
            if (!current_namespace->state.block_stack.items.empty()) {
                std::println(stderr, "Error line {}: unclosed block inside NAMESPACE '{}'",
                             compiler_state.lineno, current_namespace->name);
                result = 1;
                break;
            }
            compiler_state.uid = current_namespace->state.uid;
            compiler_state.symbol_table.next_slot = current_namespace->state.symbol_table.next_slot;
            if (debug) {
                std::println("[BASIC] ENDNAMESPACE {}", current_namespace->name);
            }
            current_namespace = nullptr;
            continue;
        }
        if (blackbox::tools::starts_with_ci(s, "FUNC ")) {
            if (current_func) {
                std::println(stderr, "Error line {}: nested FUNC is not allowed",
                             compiler_state.lineno);
                result = 1;
                break;
            }

            const char* p = skip_ws(s + 5);
            const char* colon = strchr(p, ':');
            if (!colon) {
                std::println(stderr, "Syntax error line {}: expected FUNC <name>: ...",
                             compiler_state.lineno);
                result = 1;
                break;
            }
            std::string func_name = trim_copy(std::string(p, static_cast<size_t>(colon - p)));
            if (func_name.empty()) {
                std::println(stderr, "Syntax error line {}: expected FUNC <name>: ...",
                             compiler_state.lineno);
                result = 1;
                break;
            }

            for (auto& f : funcs) {
                if (f.name == func_name) {
                    std::println(stderr, "Error line {}: function '{}' already defined",
                                 compiler_state.lineno, func_name);
                    result = 1;
                    break;
                }
            }
            if (current_namespace) {
                for (auto& f : current_namespace->funcs) {
                    if (f.name == current_namespace->name + "__" + func_name) {
                        std::println(
                            stderr,
                            "Error line {}: function '{}' already defined in namespace '{}'",
                            compiler_state.lineno, func_name, current_namespace->name);
                        result = 1;
                        break;
                    }
                }
                if (result) {
                    break;
                }
            }
            if (result) {
                break;
            }

            // push a new FuncDef
            std::vector<FuncDef>& target_funcs =
                current_namespace ? current_namespace->funcs : funcs;
            target_funcs.emplace_back();
            current_func = &target_funcs.back();
            current_func->name =
                current_namespace ? (current_namespace->name + "__" + func_name) : func_name;
            current_func->state.debug = debug;
            current_func->state.uid = compiler_state.uid;
            current_func->state.funcs = current_namespace ? &current_namespace->funcs : &funcs;
            current_func->state.in_func = true;
            current_func->state.current_namespace =
                current_namespace ? current_namespace->name : "";
            current_func->state.parent_ns_state =
                current_namespace ? &current_namespace->state : nullptr;

            p = skip_ws(colon + 1);
            while (*p) {
                int is_str = 0;
                int is_ref = 0;

                if (*p == '&') {
                    is_ref = 1;
                    p = skip_ws(p + 1);
                } else if (blackbox::tools::starts_with_ci(p, "VAR") &&
                           !(isalnum(static_cast<unsigned char>(p[3])) || p[3] == '_')) {
                    p = skip_ws(p + 3);
                } else if (blackbox::tools::starts_with_ci(p, "STR") &&
                           !(isalnum(static_cast<unsigned char>(p[3])) || p[3] == '_')) {
                    is_str = 1;
                    p = skip_ws(p + 3);
                } else {
                    std::println(stderr,
                                 "Syntax error line {}: expected VAR, STR, or & before param name",
                                 compiler_state.lineno);
                    result = 1;
                    break;
                }

                std::string param_name;
                if (!parse_identifier(p, &p, param_name)) {
                    std::println(stderr, "Syntax error line {}: expected parameter name",
                                 compiler_state.lineno);
                    result = 1;
                    break;
                }

                current_func->params.push_back(param_name);
                current_func->param_is_ref.push_back(is_ref);

                if (is_ref) {
                    current_func->state.sym_add_ref(param_name.data());
                } else if (is_str) {
                    char placeholder[64];
                    snprintf(placeholder, sizeof(placeholder), "_fparam_%s", param_name.data());
                    current_func->state.sym_add_str(param_name.data(), placeholder, 0);
                } else {
                    current_func->state.sym_add_int(param_name.data());
                }

                p = skip_ws(p);
                if (*p == ',') {
                    p = skip_ws(p + 1);
                    continue;
                }
                if (*p == '\0') {
                    break;
                }
                std::println(stderr, "Syntax error line {}: expected ',' between parameters",
                             compiler_state.lineno);
                result = 1;
                break;
            }
            if (result) {
                break;
            }

            if (debug) {
                std::println("[BASIC] FUNC {} ({} params)", func_name, current_func->params.size());
            }
            continue;
        }

        if (blackbox::tools::equals_ci(s, "ENDFUNC")) {
            if (!current_func) {
                std::println(stderr, "Error line {}: ENDFUNC without FUNC", compiler_state.lineno);
                result = 1;
                break;
            }
            if (!current_func->state.block_stack.items.empty()) {
                std::println(stderr, "Error line {}: unclosed block inside FUNC '{}'",
                             compiler_state.lineno, current_func->name);
                result = 1;
                break;
            }
            compiler_state.uid = current_func->state.uid;
            if (debug) {
                std::println("[BASIC] ENDFUNC {}", current_func->name);
            }
            current_func = nullptr;
            continue;
        }

        CompilerState* active = current_func        ? &current_func->state
                                : current_namespace ? &current_namespace->state
                                                    : &compiler_state;
        active->lineno = compiler_state.lineno;
        active->set_emit_context(s);

        if (blackbox::tools::equals_ci(s, "ASM:")) {
            in_asm_block = true;
            continue;
        }
        if (blackbox::tools::equals_ci(s, "ENDASM")) {
            in_asm_block = false;
            continue;
        }

        if (in_asm_block) {
            EMIT_CODE_META(active, "inline ASM block", "%s", s);
            continue;
        }

        if (active->compile_line(s)) {
            result = 1;
            break;
        }

        if (current_func) {
            compiler_state.uid = current_func->state.uid;
        } else {
        }
    }

    fclose(in);

    if (current_func && result == 0) {
        std::println(stderr, "Error: unterminated FUNC '{}'", current_func->name);
        result = 1;
    }
    if (current_namespace && result == 0) {
        std::println(stderr, "Error: unterminated NAMESPACE '{}'", current_namespace->name);
        result = 1;
    }
    if (!compiler_state.block_stack.items.empty() && result == 0) {
        std::println(stderr, "Error: unclosed block ({})",
                     compiler_state.block_stack.items.back().kind == BLOCK_IF      ? "IF"
                     : compiler_state.block_stack.items.back().kind == BLOCK_WHILE ? "WHILE"
                                                                                   : "FOR");
        result = 1;
    }
    if (result != 0) {
        return result;
    }

    FILE* out = fopen(output_file, "w");
    if (!out) {
        perror("fopen output");
        return 1;
    }

    std::println(out, "%asm");

    bool any_data = !compiler_state.ob.data_sec.empty();
    for (auto& ns : namespaces) {
        if (!ns.state.ob.data_sec.empty()) {
            any_data = true;
            break;
        }
        for (auto& f : ns.funcs) {
            if (!f.state.ob.data_sec.empty()) {
                any_data = true;
                break;
            }
        }
    }
    for (auto& f : funcs) {
        if (!f.state.ob.data_sec.empty()) {
            any_data = true;
            break;
        }
    }
    if (any_data) {

        std::println(out, "%data");
        fwrite(compiler_state.ob.data_sec.data(), 1, compiler_state.ob.data_sec.size(), out);
        for (auto& ns : namespaces) {
            fwrite(ns.state.ob.data_sec.data(), 1, ns.state.ob.data_sec.size(), out);
            for (auto& f : ns.funcs) {
                fwrite(f.state.ob.data_sec.data(), 1, f.state.ob.data_sec.size(), out);
            }
        }
        for (auto& f : funcs) {
            fwrite(f.state.ob.data_sec.data(), 1, f.state.ob.data_sec.size(), out);
        }
    }

    std::println(out, "%main");
    std::println(out, "    CALL __bbx_basic_main");
    std::println(out, "    HALT OK");

    if (!compiler_state.entry_point_declared) {
        std::println(out, ".__bbx_basic_main:");
        if (compiler_state.symbol_table.next_slot > 0) {
            std::println(out, "    FRAME {}", compiler_state.symbol_table.next_slot);
        }
        fwrite(compiler_state.ob.code_sec.data(), 1, compiler_state.ob.code_sec.size(), out);
        for (auto& ns : namespaces) {
            fwrite(ns.state.ob.code_sec.data(), 1, ns.state.ob.code_sec.size(), out);
        }
        std::println(out, "    RET");
    } else {
        if (compiler_state.symbol_table.next_slot > 0) {
            const char* entry_label = ".__bbx_basic_main:\n";
            std::string code(compiler_state.ob.code_sec);
            size_t pos = code.find(entry_label);
            if (pos == std::string::npos) {
                std::println(stderr, "Internal error: @ENTRY label missing from code section");
                fclose(out);
                return 1;
            }
            size_t insert_pos = pos + strlen(entry_label);
            fwrite(code.data(), 1, insert_pos, out);
            std::println(out, "    FRAME {}", compiler_state.symbol_table.next_slot);
            fwrite(code.data() + insert_pos, 1, code.size() - insert_pos, out);
        } else {
            fwrite(compiler_state.ob.code_sec.data(), 1, compiler_state.ob.code_sec.size(), out);
        }
        std::println(out, "    RET");
    }

    for (auto& f : funcs) {
        std::println(out, ".__bbx_func_{}:", f.name);
        if (f.state.symbol_table.next_slot > 0) {
            std::println(out, "    FRAME {}", f.state.symbol_table.next_slot);
        }
        for (int i = static_cast<int>(f.params.size()) - 1; i >= 0; i--) {
            Variable* v = f.state.sym_find(f.params[i].data());
            if (!v) {
                continue; // shouldn't happen
            }
            int reg = f.state.ralloc_acquire();
            if (reg < 0) {
                std::println(stderr, "Out of scratch registers in FUNC '{}' prologue", f.name);
                fclose(out);
                return 1;
            }
            char rn[4];
            reg_name(reg, rn);
            std::println(out, "    POP {}", rn);
            std::println(out, "    STOREVAR {}, {}", rn, v->slot);
            f.state.ralloc_release(reg);
        }
        fwrite(f.state.ob.code_sec.data(), 1, f.state.ob.code_sec.size(), out);
        std::println(out, "    RET");
    }
    for (auto& ns : namespaces) {
        for (auto& f : ns.funcs) {
            std::println(out, ".__bbx_func_{}:", f.name); // already mangled
            if (f.state.symbol_table.next_slot > 0) {
                std::println(out, "    FRAME {}", f.state.symbol_table.next_slot);
            }
            for (int i = static_cast<int>(f.params.size()) - 1; i >= 0; i--) {
                Variable* v = f.state.sym_find(f.params[i].data());
                if (!v) {
                    continue;
                }
                int reg = f.state.ralloc_acquire();
                if (reg < 0) {
                    std::println(stderr, "Out of scratch registers in NAMESPACE FUNC '{}' prologue",
                                 f.name);
                    fclose(out);
                    return 1;
                }
                char rn[4];
                reg_name(reg, rn);
                std::println(out, "    POP {}", rn);
                std::println(out, "    STOREVAR {}, {}", rn, v->slot);
                f.state.ralloc_release(reg);
            }
            fwrite(f.state.ob.code_sec.data(), 1, f.state.ob.code_sec.size(), out);
            std::println(out, "    RET");
        }
    }

    fclose(out);

    if (debug) {
        std::println("[BASIC] Emitted {} data bytes, {} code bytes, {} slots, {} functions",
                     compiler_state.ob.data_sec.size(), compiler_state.ob.code_sec.size(),
                     compiler_state.symbol_table.next_slot, funcs.size());
    }
    return 0;
}