#include "basic.h"
#include "../define.h"
#include "../tools.h"

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void copy_cstr(char* dst, size_t dst_size, const char* src) {
    if (dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && isspace((unsigned char) s[start])) {
        start++;
    }
    size_t end = s.size();
    while (end > start && isspace((unsigned char) s[end - 1])) {
        end--;
    }
    return s.substr(start, end - start);
}

static void reg_name(int reg, char* buf) {
    snprintf(buf, 4, "R%02d", reg);
}

static const char* skip_ws(const char* s) {
    while (*s && isspace((unsigned char) *s)) {
        s++;
    }
    return s;
}

static size_t skip_ws(const std::string& s, size_t pos) {
    while (pos < s.size() && isspace((unsigned char) s[pos])) {
        pos++;
    }
    return pos;
}

static bool parse_identifier(const char* p, const char** next, std::string& out) {
    size_t n = 0;
    while (isalnum((unsigned char) p[n]) || p[n] == '_') {
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

static bool parse_identifier(const std::string& s, size_t& pos, std::string& out) {
    size_t start = pos;
    while (pos < s.size() && (isalnum((unsigned char) s[pos]) || s[pos] == '_')) {
        pos++;
    }
    if (pos == start) {
        return false;
    }
    out = s.substr(start, pos - start);
    return true;
}

static bool parse_quoted_string(const std::string& s, size_t& pos, std::string& out) {
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

static bool parse_file_mode(const std::string& s, size_t& pos, std::string& mode_out) {
    pos = skip_ws(s, pos);
    if (pos >= s.size()) {
        return false;
    }
    if (s[pos] == '"') {
        return parse_quoted_string(s, pos, mode_out);
    }
    size_t start = pos;
    while (pos < s.size() && (isalnum((unsigned char) s[pos]) || s[pos] == '_')) {
        pos++;
    }
    if (pos == start || pos - start >= 8) {
        return false;
    }
    mode_out = s.substr(start, pos - start);
    return true;
}

static const char* find_keyword_token(const char* s, const char* kw) {
    size_t n = strlen(kw);
    for (const char* p = s; *p; p++) {
        if (!blackbox::tools::starts_with_ci(p, kw)) {
            continue;
        }
        int left_ok = (p == s) || !(isalnum((unsigned char) p[-1]) || p[-1] == '_');
        int right_ok = !(isalnum((unsigned char) p[n]) || p[n] == '_');
        if (left_ok && right_ok) {
            return p;
        }
    }
    return nullptr;
}

static const char* block_kind_name(BlockKind kind) {
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

static int get_file_handle_index(const std::vector<FileHandle>& handles, const char* name) {
    for (size_t i = 0; i < handles.size(); i++) {
        if (handles[i].name == name) {
            return (int) i;
        }
    }
    return -1;
}

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

    CompilerState() {
        st.next_slot = 0;
        st.next_data_id = 0;
        ra.used = 0;
        bs.items.reserve(64);
    }

    Variable* sym_find(const char* name) {
        for (auto& v : st.vars) {
            if (strcmp(v.name, name) == 0) {
                return &v;
            }
        }
        return nullptr;
    }

    Variable* sym_add_int(const char* name) {
        st.vars.emplace_back();
        Variable* var = &st.vars.back();
        std::memset(var, 0, sizeof(*var));
        copy_cstr(var->name, sizeof(var->name), name);
        var->type = VAR_INT;
        var->slot = st.next_slot++;
        return var;
    }

    Variable* sym_add_str(const char* name, const char* data_name, int is_const) {
        st.vars.emplace_back();
        Variable* var = &st.vars.back();
        std::memset(var, 0, sizeof(*var));
        copy_cstr(var->name, sizeof(var->name), name);
        var->type = VAR_STR;
        var->is_const = is_const;
        if (!is_const) {
            var->slot = st.next_slot++;
        }
        copy_cstr(var->data_name, sizeof(var->data_name), data_name);
        return var;
    }

    int ralloc_acquire() {
        for (int i = 0; i < SCRATCH_COUNT; i++) {
            if (!(ra.used & (1u << i))) {
                ra.used |= (1u << i);
                return SCRATCH_MIN + i;
            }
        }
        return -1;
    }

    void ralloc_release(int reg) {
        if (reg >= SCRATCH_MIN && reg <= SCRATCH_MAX) {
            ra.used &= ~(1u << (reg - SCRATCH_MIN));
        }
    }
    void set_emit_context(const char* stmt) {
        if (!stmt) {
            stmt = "";
        }
        std::string snippet(stmt);
        if (snippet.size() > 191) {
            snippet.resize(191);
        }
        if (!bs.items.empty()) {
            const Block* top = &bs.items.back();
            if (top->kind == BLOCK_FOR && top->for_var_name[0] != '\0') {
                snprintf(emit_ctx, sizeof(emit_ctx), "BASIC line %d | block=%s(%s) | src=%s",
                         lineno, block_kind_name(top->kind), top->for_var_name, snippet.data());
            } else {
                snprintf(emit_ctx, sizeof(emit_ctx), "BASIC line %d | block=%s | src=%s", lineno,
                         block_kind_name(top->kind), snippet.data());
            }
            return;
        }
        snprintf(emit_ctx, sizeof(emit_ctx), "BASIC line %d | src=%s", lineno, snippet.data());
    }
    int emit_data(const char* fmt, ...) {
        char tmp[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(tmp, sizeof(tmp), fmt, ap);
        va_end(ap);
        ob.data_sec += tmp;
        ob.data_sec += '\n';
        return 0;
    }

    int emit_code_comment(const char* detail, const char* fmt, ...) {
        char ins[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(ins, sizeof(ins), fmt, ap);
        va_end(ap);
        char line_buf[1024];
        if (detail && *detail) {
            snprintf(line_buf, sizeof(line_buf), "%s ; %s | %s\n\n", ins,
                     emit_ctx[0] ? emit_ctx : "BASIC", detail);
        } else {
            snprintf(line_buf, sizeof(line_buf), "%s ; %s\n\n", ins,
                     emit_ctx[0] ? emit_ctx : "BASIC");
        }
        ob.code_sec += line_buf;
        return 0;
    }

    int get_file_handle_fd(const char* name, uint8_t* out_fd) {
        int idx = get_file_handle_index(file_handles, name);
        if (idx < 0) {
            fprintf(stderr, "Undefined file handle '%s' on line %d\n", name, lineno);
            return 1;
        }
        *out_fd = file_handles[idx].fd;
        return 0;
    }

    int alloc_file_handle_fd(const char* name, uint8_t* out_fd) {
        int idx = get_file_handle_index(file_handles, name);
        if (idx >= 0) {
            *out_fd = file_handles[idx].fd;
            return 0;
        }
        if (next_file_handle >= FILE_DESCRIPTORS) {
            fprintf(stderr, "Too many file handles\n");
            return 1;
        }
        file_handles.push_back(FileHandle());
        file_handles.back().name = name;
        file_handles.back().fd = next_file_handle;
        *out_fd = next_file_handle++;
        return 0;
    }

    void bstack_push(Block b) { bs.items.push_back(b); }
    Block* bstack_peek() { return bs.items.empty() ? nullptr : &bs.items.back(); }
    Block bstack_pop() {
        Block b = bs.items.back();
        bs.items.pop_back();
        return b;
    }

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

int CompilerState::emit_atom(const char* s, const char** end, int* out_reg) {
    s = skip_ws(s);

    if (*s == '(') {
        s++;
        if (emit_expr(s, out_reg)) {
            return 1;
        }
        s = skip_ws(*end);
        if (*s != ')') {
            fprintf(stderr, "Expected ')' in expression\n");
            return 1;
        }
        *end = s + 1;
        return 0;
    }

    if (*s == '-' || isdigit((unsigned char) *s)) {
        char* endptr;
        int32_t val = (int32_t) strtol(s, &endptr, 10);
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of registers\n");
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

    if (isalpha((unsigned char) *s) || *s == '_') {
        std::string name;
        const char* next = s;
        if (!parse_identifier(s, &next, name)) {
            fprintf(stderr, "Expression error: expected identifier\n");
            return 1;
        }
        *end = next;
        Variable* v = sym_find(name.data());
        if (!v) {
            fprintf(stderr, "Undefined variable '%s'\n", name.data());
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
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
        return 0;
    }

    fprintf(stderr, "Expression error: unexpected character '%c'\n", *s);
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
            (q == p || !(isalnum((unsigned char) q[-1]) || q[-1] == '_')) &&
            !(isalnum((unsigned char) q[2]) || q[2] == '_')) {

            std::string left(p, (size_t) (q - p));
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

    while (1) {
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
            fprintf(stderr, "Condition error: expected comparison operator near '%s'\n", p);
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
                     !(isalnum((unsigned char) next[3]) || next[3] == '_');

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

        if (isalpha((unsigned char) *arg_start) || *arg_start == '_') {
            std::string name;
            const char* p = arg_start;
            if (!parse_identifier(arg_start, &p, name)) {
                fprintf(stderr, "Syntax error line %d: expected identifier in %s\n", lineno,
                        stmt_name);
                return 1;
            }
            if (*skip_ws(p) == '\0' || *skip_ws(p) == ',') {
                Variable* v = sym_find(name.data());
                if (v && v->type == VAR_STR) {
                    RegGuard rg(&ra);
                    if (!rg.ok()) {
                        fprintf(stderr, "Out of scratch registers\n");
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
                        printf("[BASIC] %s %s\n", stmt_name, name.data());
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
                fprintf(stderr, "Syntax error line %d: unterminated string in %s\n", lineno,
                        stmt_name);
                return 1;
            }
            char data_name[64];
            snprintf(data_name, sizeof(data_name), "_p%lu", uid++);
            EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name, (int) (str_end - str_start),
                      str_start);
            RegGuard rg(&ra);
            if (!rg.ok()) {
                fprintf(stderr, "Out of scratch registers\n");
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
                printf("[BASIC] %s \"%.*s\"\n", stmt_name, (int) (str_end - str_start), str_start);
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
                printf("[BASIC] %s %.*s\n", stmt_name, (int) (expr_end - arg), arg);
            }
            arg = skip_ws(expr_end);
        }

    next_arg:
        if (*arg == ',') {
            arg = skip_ws(arg + 1);
            if (*arg == '\0') {
                fprintf(stderr, "Syntax error line %d: expected value after ',' in %s\n", lineno,
                        stmt_name);
                return 1;
            }
            continue;
        }
        if (*arg != '\0') {
            fprintf(stderr, "Syntax error line %d: expected ',' between %s values\n", lineno,
                    stmt_name);
            return 1;
        }
    }
    return 0;
}

int CompilerState::compile_line(char* s) {

    // CONST
    if (blackbox::tools::starts_with_ci(s, "CONST ")) {
        const char* eq = strchr(s + 6, '=');
        if (!eq) {
            fprintf(stderr, "Syntax error line %d: expected CONST <n> = <value>\n", lineno);
            return 1;
        }
        std::string name = trim_copy(std::string(s + 6, (size_t) (eq - (s + 6))));
        if (name.empty()) {
            fprintf(stderr, "Syntax error line %d: expected CONST <n> = <value>\n", lineno);
            return 1;
        }
        if (sym_find(name.data())) {
            fprintf(stderr, "Error line %d: variable '%s' already defined\n", lineno, name.data());
            return 1;
        }
        std::string rhs = trim_copy(eq + 1);
        if (!rhs.empty() && rhs[0] == '"') {
            const char* str_start = rhs.data() + 1;
            const char* str_end = strchr(str_start, '"');
            if (!str_end) {
                fprintf(stderr, "Syntax error line %d: unterminated string literal\n", lineno);
                return 1;
            }
            char data_name[64];
            snprintf(data_name, sizeof(data_name), "_s%lu_%s", uid++, name.data());
            EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name, (int) (str_end - str_start),
                      str_start);
            sym_add_str(name.data(), data_name, 1);
            if (debug) {
                printf("[BASIC] CONST string %s -> $%s\n", name.data(), data_name);
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
                printf("[BASIC] CONST int %s -> slot %u\n", name.data(), v->slot);
            }
        }
        return 0;
    }

    // VAR
    if (blackbox::tools::starts_with_ci(s, "VAR ")) {
        const char* eq = strchr(s + 4, '=');
        if (!eq) {
            fprintf(stderr, "Syntax error line %d: expected VAR <n> = <value>\n", lineno);
            return 1;
        }
        std::string name = trim_copy(std::string(s + 4, (size_t) (eq - (s + 4))));
        std::string rhs = trim_copy(eq + 1);
        if (name.empty()) {
            fprintf(stderr, "Syntax error line %d: expected VAR <n> = <value>\n", lineno);
            return 1;
        }
        if (!rhs.empty() && rhs[0] == '"') {
            const char* str_start = rhs.data() + 1;
            const char* str_end = strchr(str_start, '"');
            if (!str_end) {
                fprintf(stderr, "Syntax error line %d: unterminated string\n", lineno);
                return 1;
            }
            char data_name[64];
            snprintf(data_name, sizeof(data_name), "_s%lu_%s", uid++, name.data());
            EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name, (int) (str_end - str_start),
                      str_start);
            Variable* v = sym_add_str(name.data(), data_name, 0);
            RegGuard rg(&ra);
            if (!rg.ok()) {
                fprintf(stderr, "Out of scratch registers\n");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            EMIT_CODE(this, "    LOADSTR $%s, %s", data_name, rn);
            EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
            if (debug) {
                printf("[BASIC] VAR string %s -> $%s\n", name.data(), data_name);
            }
        } else {
            if (sym_find(name.data())) {
                fprintf(stderr, "Error line %d: variable '%s' already defined\n", lineno,
                        name.data());
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
                printf("[BASIC] VAR int %s -> slot %u\n", name.data(), v->slot);
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
            size_t lhslen = (size_t) (eq - s);
            if (lhslen < 64) {
                std::string name = trim_copy(std::string(s, lhslen));
                Variable* v = sym_find(name.data());
                if (v) {
                    if (v->is_const) {
                        fprintf(stderr, "Error line %d: cannot assign to CONST '%s'\n", lineno,
                                name.data());
                        return 1;
                    }
                    std::string rhs = trim_copy(eq + 1);
                    if (v->type == VAR_STR) {
                        if (rhs.empty() || rhs[0] != '"') {
                            fprintf(stderr,
                                    "Type error line %d: expected string literal for '%s'\n",
                                    lineno, name.data());
                            return 1;
                        }
                        const char* str_start = rhs.data() + 1;
                        const char* str_end = strchr(str_start, '"');
                        if (!str_end) {
                            fprintf(stderr, "Syntax error line %d: unterminated string\n", lineno);
                            return 1;
                        }
                        if (*skip_ws(str_end + 1) != '\0') {
                            fprintf(
                                stderr,
                                "Syntax error line %d: unexpected trailing tokens after string\n",
                                lineno);
                            return 1;
                        }
                        char data_name[64];
                        snprintf(data_name, sizeof(data_name), "_s%lu_%s", uid++, name.data());
                        EMIT_DATA(this, "    STR $%s, \"%.*s\"", data_name,
                                  (int) (str_end - str_start), str_start);
                        copy_cstr(v->data_name, sizeof(v->data_name), data_name);
                        RegGuard rg(&ra);
                        char rn[4];
                        reg_name(rg, rn);
                        EMIT_CODE(this, "    LOADSTR $%s, %s", data_name, rn);
                        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
                        if (debug) {
                            printf("[BASIC] ASSIGN string %s -> $%s\n", name.data(), data_name);
                        }
                        return 0;
                    }
                    if (v->type != VAR_INT) {
                        fprintf(
                            stderr,
                            "Type error line %d: cannot assign to string '%s' with numeric expr\n",
                            lineno, name.data());
                        return 1;
                    }
                    int ereg;
                    if (emit_expr(rhs.data(), &ereg)) {
                        return 1;
                    }
                    char ern[4];
                    reg_name(ereg, ern);
                    EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", ern, v->slot);
                    ralloc_release(ereg);
                    if (debug) {
                        printf("[BASIC] ASSIGN %s -> slot %u\n", name.data(), v->slot);
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
            printf("[BASIC] IF condition, skip to %s if false\n", b.else_label);
        }
        return 0;
    }

    // ELSE IF
    if (blackbox::tools::starts_with_ci(s, "ELSE ")) {
        const char* p = skip_ws(s + 5);
        if (blackbox::tools::starts_with_ci(p, "IF ")) {
            Block b = bstack_pop();
            if (b.kind != BLOCK_IF) {
                fprintf(stderr, "Error line %d: ELSE IF without IF\n", lineno);
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
                printf("[BASIC] ELSE IF condition, else to %s, exit to %s\n", nb.else_label,
                       nb.end_label);
            }
            return 0;
        }
    }

    // ELSE:
    if (blackbox::tools::equals_ci(s, "ELSE:")) {
        Block* b = bstack_peek();
        if (!b || b->kind != BLOCK_IF) {
            fprintf(stderr, "Error line %d: ELSE without IF\n", lineno);
            return 1;
        }
        b->has_else = 1;
        EMIT_CODE(this, "    JMP %s", b->end_label + 1);
        EMIT_CODE(this, "%s:", b->else_label);
        if (debug) {
            printf("[BASIC] ELSE\n");
        }
        return 0;
    }

    // ENDIF
    if (blackbox::tools::equals_ci(s, "ENDIF")) {
        Block b = bstack_pop();
        if (b.kind != BLOCK_IF) {
            fprintf(stderr, "Error line %d: ENDIF without IF\n", lineno);
            return 1;
        }
        if (!b.has_else) {
            EMIT_CODE(this, "%s:", b.else_label);
        }
        EMIT_CODE(this, "%s:", b.end_label);
        if (debug) {
            printf("[BASIC] ENDIF\n");
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
            printf("[BASIC] WHILE -> top=%s end=%s\n", b.loop_label, b.end_label);
        }
        return 0;
    }

    // ENDWHILE
    if (blackbox::tools::equals_ci(s, "ENDWHILE")) {
        Block b = bstack_pop();
        if (b.kind != BLOCK_WHILE) {
            fprintf(stderr, "Error line %d: ENDWHILE without WHILE\n", lineno);
            return 1;
        }
        EMIT_CODE(this, "    JMP %s", b.loop_label + 1);
        EMIT_CODE(this, "%s:", b.end_label);
        if (debug) {
            printf("[BASIC] ENDWHILE\n");
        }
        return 0;
    }

    if (blackbox::tools::starts_with_ci(s, "EWRITE")) {
        const char* arg = s + 6;
        if (*arg != '\0' && !isspace((unsigned char) *arg)) {
            fprintf(stderr, "Syntax error line %d: expected EWRITE [<value>[, ...]]\n", lineno);
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
        if (*arg != '\0' && !isspace((unsigned char) *arg)) {
            fprintf(stderr, "Syntax error line %d: expected WRITE [<value>[, ...]]\n", lineno);
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
        if (*arg != '\0' && !isspace((unsigned char) *arg)) {
            fprintf(stderr, "Syntax error line %d: expected EPRINT [<value>[, ...]]\n", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg != '\0' && emit_write_values(arg, "EPRINT", 1)) {
            return 1;
        }
        {
            RegGuard rg(&ra);
            if (!rg.ok()) {
                fprintf(stderr, "Out of scratch registers\n");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            EMIT_CODE(this, "    MOVI %s, 10", rn);
            EMIT_CODE(this, "    EPRINTCHAR %s", rn);
        }
        if (debug) {
            printf("[BASIC] EPRINT <newline>\n");
        }
        return 0;
    }
    if (blackbox::tools::starts_with_ci(s, "PRINT")) {
        const char* arg = s + 5;
        if (*arg != '\0' && !isspace((unsigned char) *arg)) {
            fprintf(stderr, "Syntax error line %d: expected PRINT [<value>[, ...]]\n", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg != '\0' && emit_write_values(arg, "PRINT", 0)) {
            return 1;
        }
        {
            RegGuard rg(&ra);
            if (!rg.ok()) {
                fprintf(stderr, "Out of scratch registers\n");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            EMIT_CODE(this, "    MOVI %s, 10", rn);
            EMIT_CODE(this, "    PRINTCHAR %s", rn);
        }
        if (debug) {
            printf("[BASIC] PRINT <newline>\n");
        }
        return 0;
    }

    // SLEEP
    if (blackbox::tools::starts_with_ci(s, "SLEEP")) {
        const char* arg = s + 5;
        if (*arg != '\0' && !isspace((unsigned char) *arg)) {
            fprintf(stderr, "Syntax error line %d: expected SLEEP <expr>\n", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg == '\0') {
            fprintf(stderr, "Syntax error line %d: expected SLEEP <expr>\n", lineno);
            return 1;
        }
        const char* expr_end = nullptr;
        int reg;
        if (emit_expr_p(arg, &expr_end, &reg)) {
            return 1;
        }
        if (*skip_ws(expr_end) != '\0') {
            ralloc_release(reg);
            fprintf(stderr, "Syntax error line %d: SLEEP takes a single expression\n", lineno);
            return 1;
        }
        char rn[4];
        reg_name(reg, rn);
        EMIT_CODE(this, "    SLEEP %s", rn);
        ralloc_release(reg);
        if (debug) {
            printf("[BASIC] SLEEP %s\n", arg);
        }
        return 0;
    }

    // HALT
    if (blackbox::tools::starts_with_ci(s, "HALT")) {
        const char* halt_suffix = s + 4;
        if (*halt_suffix != '\0' && !std::isspace((unsigned char) *halt_suffix)) {
            fprintf(stderr, "Syntax error line %d: expected HALT [OK|BAD|<number>]\n", lineno);
            return 1;
        }
        std::string arg = trim_copy(halt_suffix);
        if (arg.empty()) {
            EMIT_CODE(this, "    HALT");
            if (debug) {
                printf("[BASIC] HALT\n");
            }
            return 0;
        }
        size_t tok_len = 0;
        while (tok_len < arg.size() && !std::isspace((unsigned char) arg[tok_len])) {
            tok_len++;
        }
        std::string token = arg.substr(0, tok_len);
        if (!trim_copy(arg.substr(tok_len)).empty()) {
            fprintf(stderr, "Syntax error line %d: HALT takes at most one operand\n", lineno);
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
                fprintf(stderr, "Syntax error line %d: invalid HALT operand '%s'\n", lineno,
                        token.data());
                return 1;
            }
            EMIT_CODE(this, "    HALT %lu", v);
        }
        if (debug) {
            printf("[BASIC] HALT %s\n", token.data());
        }
        return 0;
    }

    // LABEL
    if (blackbox::tools::starts_with_ci(s, "LABEL ")) {
        std::string name = trim_copy(s + 6);
        EMIT_CODE(this, ".%s:", name.data());
        if (debug) {
            printf("[BASIC] LABEL %s\n", name.data());
        }
        return 0;
    }

    // GOTO
    if (blackbox::tools::starts_with_ci(s, "GOTO ")) {
        std::string name = trim_copy(s + 5);
        EMIT_CODE(this, "    JMP %s", name.data());
        if (debug) {
            printf("[BASIC] GOTO %s\n", name.data());
        }
        return 0;
    }

    // CALL
    if (blackbox::tools::starts_with_ci(s, "CALL ")) {
        std::string name = trim_copy(s + 5);
        EMIT_CODE(this, "    CALL %s", name.data());
        if (debug) {
            printf("[BASIC] CALL %s\n", name.data());
        }
        return 0;
    }

    // RETURN
    if (blackbox::tools::equals_ci(s, "RETURN")) {
        EMIT_CODE(this, "    RET");
        if (debug) {
            printf("[BASIC] RETURN\n");
        }
        return 0;
    }

    // EXEC
    if (blackbox::tools::starts_with_ci(s, "EXEC")) {
        const char* arg = s + 4;
        if (*arg != '\0' && !isspace((unsigned char) *arg)) {
            fprintf(stderr, "Syntax error line %d: expected EXEC \"<cmd>\", <var>\n", lineno);
            return 1;
        }
        arg = skip_ws(arg);
        if (*arg != '"') {
            fprintf(stderr, "Syntax error line %d: expected EXEC \"<cmd>\", <var>\n", lineno);
            return 1;
        }
        const char* str_start = arg + 1;
        const char* str_end = strchr(str_start, '"');
        if (!str_end) {
            fprintf(stderr, "Syntax error line %d: missing closing quote for EXEC\n", lineno);
            return 1;
        }
        std::string cmd(str_start, (size_t) (str_end - str_start));
        if (cmd.size() > 255) {
            cmd.resize(255);
        }
        const char* after = skip_ws(str_end + 1);
        int have_dest = 0;
        Variable* dv = nullptr;
        if (*after != '\0') {
            if (*after != ',') {
                fprintf(stderr, "Syntax error line %d: expected ',' before EXEC destination\n",
                        lineno);
                return 1;
            }
            after = skip_ws(after + 1);
            std::string varname;
            const char* var_end = after;
            if (!parse_identifier(after, &var_end, varname)) {
                fprintf(stderr, "Syntax error line %d: expected EXEC destination variable\n",
                        lineno);
                return 1;
            }
            after = skip_ws(var_end);
            if (*after != '\0') {
                fprintf(stderr, "Syntax error line %d: unexpected tokens after EXEC destination\n",
                        lineno);
                return 1;
            }
            dv = sym_find(varname.data());
            if (!dv) {
                fprintf(stderr, "Undefined variable '%s' on line %d\n", varname.data(), lineno);
                return 1;
            }
            if (dv->is_const) {
                fprintf(stderr, "Error line %d: cannot assign to CONST '%s'\n", lineno,
                        varname.data());
                return 1;
            }
            if (dv->type != VAR_INT) {
                fprintf(stderr, "Type error line %d: EXEC destination '%s' must be integer\n",
                        lineno, varname.data());
                return 1;
            }
            have_dest = 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
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
                printf("[BASIC] EXEC \"%s\" -> %s\n", cmd.data(), dv->name);
            } else {
                printf("[BASIC] EXEC \"%s\"\n", cmd.data());
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
            fprintf(stderr, "Syntax error line %d: expected FOPEN <mode>, <handle>, \"<file>\"\n",
                    lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            fprintf(stderr, "Syntax error line %d: expected FOPEN <mode>, <handle>, \"<file>\"\n",
                    lineno);
            return 1;
        }
        pos = skip_ws(arg, pos + 1);
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            fprintf(stderr, "Syntax error line %d: expected FOPEN <mode>, <handle>, \"<file>\"\n",
                    lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            fprintf(stderr, "Syntax error line %d: expected FOPEN <mode>, <handle>, \"<file>\"\n",
                    lineno);
            return 1;
        }
        pos = skip_ws(arg, pos + 1);
        std::string filename;
        if (!parse_quoted_string(arg, pos, filename)) {
            fprintf(stderr, "Syntax error line %d: expected FOPEN <mode>, <handle>, \"<file>\"\n",
                    lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos != arg.size()) {
            fprintf(stderr, "Syntax error line %d: unexpected trailing tokens in FOPEN\n", lineno);
            return 1;
        }
        Variable* v = sym_find(handle_name.data());
        if (!v) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", handle_name.data(), lineno);
            return 1;
        }
        if (v->is_const) {
            fprintf(stderr, "Error line %d: cannot use CONST '%s' as file handle\n", lineno,
                    handle_name.data());
            return 1;
        }
        if (v->type != VAR_INT) {
            fprintf(stderr, "Type error line %d: file handle '%s' must be integer\n", lineno,
                    handle_name.data());
            return 1;
        }
        uint8_t fd;
        if (alloc_file_handle_fd(handle_name.data(), &fd)) {
            return 1;
        }
        EMIT_CODE(this, "    FOPEN %s, F%u, \"%s\"", mode.data(), fd, filename.data());
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    MOVI %s, %u", rn, fd);
        EMIT_CODE_META(this, v->name, "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            printf("[BASIC] FOPEN %s -> %s\n", filename.data(), handle_name.data());
        }
        return 0;
    }

    // FCLOSE
    if (blackbox::tools::starts_with_ci(s, "FCLOSE")) {
        std::string arg = trim_copy(s + 6);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            fprintf(stderr, "Syntax error line %d: expected FCLOSE <handle>\n", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos != arg.size()) {
            fprintf(stderr, "Syntax error line %d: FCLOSE takes exactly one operand\n", lineno);
            return 1;
        }
        uint8_t fd;
        if (get_file_handle_fd(handle_name.data(), &fd)) {
            return 1;
        }
        EMIT_CODE(this, "    FCLOSE F%u", fd);
        if (debug) {
            printf("[BASIC] FCLOSE %s\n", handle_name.data());
        }
        return 0;
    }

    // FREAD
    if (blackbox::tools::starts_with_ci(s, "FREAD")) {
        std::string arg = trim_copy(s + 5);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            fprintf(stderr, "Syntax error line %d: expected FREAD <handle>, <variable>\n", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            fprintf(stderr, "Syntax error line %d: expected FREAD <handle>, <variable>\n", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos + 1);
        std::string target_name;
        if (!parse_identifier(arg, pos, target_name)) {
            fprintf(stderr, "Syntax error line %d: expected FREAD <handle>, <variable>\n", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos != arg.size()) {
            fprintf(stderr, "Syntax error line %d: FREAD takes exactly two operands\n", lineno);
            return 1;
        }
        Variable* dest = sym_find(target_name.data());
        if (!dest) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", target_name.data(), lineno);
            return 1;
        }
        if (dest->is_const) {
            fprintf(stderr, "Error line %d: cannot assign to CONST '%s'\n", lineno,
                    target_name.data());
            return 1;
        }
        if (dest->type != VAR_INT) {
            fprintf(stderr, "Type error line %d: FREAD target '%s' must be integer\n", lineno,
                    target_name.data());
            return 1;
        }
        uint8_t fd;
        if (get_file_handle_fd(handle_name.data(), &fd)) {
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    FREAD F%u, %s", fd, rn);
        EMIT_CODE_META(this, dest->name, "    STOREVAR %s, %u", rn, dest->slot);
        if (debug) {
            printf("[BASIC] FREAD %s -> %s\n", handle_name.data(), target_name.data());
        }
        return 0;
    }

    // FWRITE
    if (blackbox::tools::starts_with_ci(s, "FWRITE")) {
        std::string arg = trim_copy(s + 6);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            fprintf(stderr, "Syntax error line %d: expected FWRITE <handle>, <expr>\n", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            fprintf(stderr, "Syntax error line %d: expected FWRITE <handle>, <expr>\n", lineno);
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
            fprintf(stderr, "Syntax error line %d: FWRITE takes a single expression\n", lineno);
            return 1;
        }
        char rn[4];
        reg_name(reg, rn);
        EMIT_CODE(this, "    FWRITE F%u, %s", fd, rn);
        ralloc_release(reg);
        if (debug) {
            printf("[BASIC] FWRITE %s\n", handle_name.data());
        }
        return 0;
    }

    // FSEEK
    if (blackbox::tools::starts_with_ci(s, "FSEEK")) {
        std::string arg = trim_copy(s + 5);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            fprintf(stderr, "Syntax error line %d: expected FSEEK <handle>, <expr>\n", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            fprintf(stderr, "Syntax error line %d: expected FSEEK <handle>, <expr>\n", lineno);
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
            fprintf(stderr, "Syntax error line %d: FSEEK takes a single expression\n", lineno);
            return 1;
        }
        char rn[4];
        reg_name(reg, rn);
        EMIT_CODE(this, "    FSEEK F%u, %s", fd, rn);
        ralloc_release(reg);
        if (debug) {
            printf("[BASIC] FSEEK %s\n", handle_name.data());
        }
        return 0;
    }

    // FPRINT
    if (blackbox::tools::starts_with_ci(s, "FPRINT")) {
        std::string arg = trim_copy(s + 6);
        size_t pos = 0;
        std::string handle_name;
        if (!parse_identifier(arg, pos, handle_name)) {
            fprintf(stderr, "Syntax error line %d: expected FPRINT <handle>, <value>\n", lineno);
            return 1;
        }
        pos = skip_ws(arg, pos);
        if (pos >= arg.size() || arg[pos] != ',') {
            fprintf(stderr, "Syntax error line %d: expected FPRINT <handle>, <value>\n", lineno);
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
                fprintf(stderr, "Syntax error line %d: unterminated string in FPRINT\n", lineno);
                return 1;
            }
            pos = skip_ws(arg, pos);
            if (pos != arg.size()) {
                fprintf(stderr, "Syntax error line %d: unexpected tokens after FPRINT string\n",
                        lineno);
                return 1;
            }
            RegGuard rg(&ra);
            if (!rg.ok()) {
                fprintf(stderr, "Out of scratch registers\n");
                return 1;
            }
            char rn[4];
            reg_name(rg, rn);
            for (unsigned char c : text) {
                EMIT_CODE(this, "    MOVI %s, %u", rn, (unsigned) c);
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
                fprintf(stderr, "Syntax error line %d: FPRINT takes a single expression\n", lineno);
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
            printf("[BASIC] FPRINT %s\n", handle_name.data());
        }
        return 0;
    }

    // INPUT
    if (blackbox::tools::starts_with_ci(s, "INPUT ")) {
        const char* name = skip_ws(s + 6);
        if (*name == '"') {
            const char* str_end = strchr(name + 1, '"');
            if (!str_end) {
                fprintf(stderr, "Syntax error line %d: unterminated string in INPUT\n", lineno);
                return 1;
            }
            std::string prompt(name, (size_t) (str_end - name + 1));
            if (emit_write_values(prompt.data(), "WRITE", 0)) {
                return 1;
            }
            name = skip_ws(str_end + 1);
            if (*name != ',') {
                fprintf(stderr, "Syntax error line %d: expected ',' after INPUT prompt\n", lineno);
                return 1;
            }
            name = skip_ws(name + 1);
        }
        Variable* v = sym_find(name);
        if (!v) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", name, lineno);
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
            printf("[BASIC] INPUT %s\n", name);
        }
        return 0;
    }

    // GETKEY
    if (blackbox::tools::starts_with_ci(s, "GETKEY")) {
        const char* p = skip_ws(s + 6);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            fprintf(stderr, "Syntax error line %d: expected GETKEY <identifier>\n", lineno);
            return 1;
        }
        if (*skip_ws(p) != '\0') {
            fprintf(stderr, "Syntax error line %d: GETKEY takes at most one operand\n", lineno);
            return 1;
        }
        Variable* v = sym_find(name.data());
        if (!v) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", name.data(), lineno);
            return 1;
        }
        if (v->is_const) {
            fprintf(stderr, "Error line %d: cannot assign to CONST '%s'\n", lineno, name.data());
            return 1;
        }
        if (v->type != VAR_INT) {
            fprintf(stderr, "Type error line %d: GETKEY target must be integer\n", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    GETKEY %s", rn);
        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            printf("[BASIC] GETKEY %s\n", name.data());
        }
        return 0;
    }

    // GETARGC
    if (blackbox::tools::starts_with_ci(s, "GETARGC")) {
        const char* p = skip_ws(s + 7);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            fprintf(stderr, "Syntax error line %d: expected GETARGC <identifier>\n", lineno);
            return 1;
        }
        if (*skip_ws(p) != '\0') {
            fprintf(stderr, "Syntax error line %d: GETARGC takes at most one operand\n", lineno);
            return 1;
        }
        Variable* v = sym_find(name.data());
        if (!v) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", name.data(), lineno);
            return 1;
        }
        if (v->is_const) {
            fprintf(stderr, "Error line %d: cannot assign to CONST '%s'\n", lineno, name.data());
            return 1;
        }
        if (v->type != VAR_INT) {
            fprintf(stderr, "Type error line %d: GETARGC target must be integer\n", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    GETARGC %s", rn);
        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            printf("[BASIC] GETARGC %s\n", name.data());
        }
        return 0;
    }

    // GETARG
    if (blackbox::tools::starts_with_ci(s, "GETARG")) {
        const char* p = skip_ws(s + 6);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            fprintf(stderr, "Syntax error line %d: expected GETARG <identifier>, <index>\n",
                    lineno);
            return 1;
        }
        p = skip_ws(p);
        if (*p != ',') {
            fprintf(stderr, "Syntax error line %d: expected GETARG <identifier>, <index>\n",
                    lineno);
            return 1;
        }
        p = skip_ws(p + 1);
        std::string idx_tok;
        if (!parse_identifier(p, &p, idx_tok)) {
            fprintf(stderr, "Syntax error line %d: expected GETARG <identifier>, <index>\n",
                    lineno);
            return 1;
        }
        if (*skip_ws(p) != '\0') {
            fprintf(stderr, "Syntax error line %d: GETARG takes exactly two operands\n", lineno);
            return 1;
        }
        char* endptr;
        unsigned long idx = strtoul(idx_tok.data(), &endptr, 0);
        if (*endptr != '\0') {
            fprintf(stderr, "Syntax error line %d: invalid GETARG index '%s'\n", lineno,
                    idx_tok.data());
            return 1;
        }
        Variable* v = sym_find(name.data());
        if (!v) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", name.data(), lineno);
            return 1;
        }
        if (v->is_const) {
            fprintf(stderr, "Error line %d: cannot assign to CONST '%s'\n", lineno, name.data());
            return 1;
        }
        if (v->type != VAR_STR) {
            fprintf(stderr, "Type error line %d: GETARG target must be string\n", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    GETARG %s, %lu", rn, idx);
        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            printf("[BASIC] GETARG %s, %lu\n", name.data(), idx);
        }
        return 0;
    }

    // GETENV
    if (blackbox::tools::starts_with_ci(s, "GETENV")) {
        const char* p = skip_ws(s + 6);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            fprintf(stderr, "Syntax error line %d: expected GETENV <identifier>, <varname>\n",
                    lineno);
            return 1;
        }
        p = skip_ws(p);
        if (*p != ',') {
            fprintf(stderr, "Syntax error line %d: expected GETENV <identifier>, <varname>\n",
                    lineno);
            return 1;
        }
        p = skip_ws(p + 1);
        std::string envname;
        if (*p == '"') {
            const char* end = strchr(p + 1, '"');
            if (!end) {
                fprintf(stderr, "Syntax error line %d: unterminated string in GETENV\n", lineno);
                return 1;
            }
            envname.assign(p + 1, (size_t) (end - (p + 1)));
            p = skip_ws(end + 1);
        } else {
            const char* next = p;
            if (!parse_identifier(p, &next, envname)) {
                fprintf(stderr, "Syntax error line %d: expected GETENV <identifier>, <varname>\n",
                        lineno);
                return 1;
            }
            p = skip_ws(next);
        }
        if (*p != '\0') {
            fprintf(stderr, "Syntax error line %d: GETENV takes exactly two operands\n", lineno);
            return 1;
        }
        Variable* v = sym_find(name.data());
        if (!v) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", name.data(), lineno);
            return 1;
        }
        if (v->is_const) {
            fprintf(stderr, "Error line %d: cannot assign to CONST '%s'\n", lineno, name.data());
            return 1;
        }
        if (v->type != VAR_STR) {
            fprintf(stderr, "Type error line %d: GETENV target must be string\n", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        EMIT_CODE(this, "    GETENV %s, \"%s\"", rn, envname.data());
        EMIT_CODE_META(this, name.data(), "    STOREVAR %s, %u", rn, v->slot);
        if (debug) {
            printf("[BASIC] GETENV %s, %s\n", name.data(), envname.data());
        }
        return 0;
    }

    // RANDOM
    if (blackbox::tools::starts_with_ci(s, "RANDOM")) {
        const char* p = skip_ws(s + 6);
        std::string name;
        if (!parse_identifier(p, &p, name)) {
            fprintf(stderr, "Syntax error line %d: expected RANDOM <identifier>\n", lineno);
            return 1;
        }
        p = skip_ws(p);
        Variable* v = sym_find(name.data());
        if (!v) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", name.data(), lineno);
            return 1;
        }
        if (v->is_const) {
            fprintf(stderr, "Error line %d: cannot assign to CONST '%s'\n", lineno, name.data());
            return 1;
        }
        if (v->type != VAR_INT) {
            fprintf(stderr, "Type error line %d: RANDOM target must be integer\n", lineno);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
            return 1;
        }
        char rn[4];
        reg_name(rg, rn);
        if (*p == ',') {
            p = skip_ws(p + 1);
            std::string range_text(p);
            size_t comma_pos = range_text.find(',');
            if (comma_pos == std::string::npos) {
                fprintf(stderr, "Syntax error line %d: expected RANDOM <n>, <min>, <max>\n",
                        lineno);
                return 1;
            }
            std::string min_str = trim_copy(range_text.substr(0, comma_pos));
            std::string max_str = trim_copy(range_text.substr(comma_pos + 1));
            if (min_str.empty() || max_str.empty()) {
                fprintf(stderr, "Syntax error line %d: expected RANDOM <n>, <min>, <max>\n",
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
            printf("[BASIC] RANDOM %s\n", name.data());
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
            !(isalnum((unsigned char) p[3]) || p[3] == '_')) {
            inline_decl = 1;
            p = skip_ws(p + 3);
        }

        std::string var_name;
        if (!parse_identifier(p, &p, var_name)) {
            fprintf(stderr, "Syntax error line %d: expected FOR [VAR] <identifier> = ...\n",
                    lineno);
            return 1;
        }
        p = skip_ws(p);
        if (*p != '=') {
            fprintf(stderr, "Syntax error line %d: expected '=' in FOR statement\n", lineno);
            return 1;
        }
        const char* init_start = skip_ws(p + 1);
        const char* to_kw = find_keyword_token(init_start, "TO");
        if (!to_kw) {
            fprintf(stderr, "Syntax error line %d: expected TO in FOR statement\n", lineno);
            return 1;
        }
        const char* to_rhs = skip_ws(to_kw + 2);
        const char* step_kw = find_keyword_token(to_rhs, "STEP");

        std::string init_expr = trim_copy(std::string(init_start, (size_t) (to_kw - init_start)));
        std::string limit_expr = trim_copy(
            std::string(to_rhs, (size_t) ((step_kw ? step_kw : to_rhs + strlen(to_rhs)) - to_rhs)));
        std::string step_expr =
            step_kw ? trim_copy(std::string(step_kw + 4,
                                            (size_t) ((to_rhs + strlen(to_rhs)) - (step_kw + 4))))
                    : std::string("1");

        if (init_expr.empty() || limit_expr.empty() || step_expr.empty()) {
            fprintf(stderr, "Syntax error line %d: FOR requires init, TO, and STEP\n", lineno);
            return 1;
        }

        Variable* v = sym_find(var_name.data());
        if (inline_decl) {
            if (v) {
                fprintf(stderr, "Error line %d: variable '%s' already defined\n", lineno,
                        var_name.data());
                return 1;
            }
            v = sym_add_int(var_name.data());
        } else if (!v) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", var_name.data(), lineno);
            return 1;
        }
        if (v->is_const) {
            fprintf(stderr, "Error line %d: cannot use CONST '%s' as FOR variable\n", lineno,
                    var_name.data());
            return 1;
        }
        if (v->type != VAR_INT) {
            fprintf(stderr, "Type error line %d: FOR control variable '%s' must be integer\n",
                    lineno, var_name.data());
            return 1;
        }

        uint32_t limit_slot = st.next_slot++;
        uint32_t step_slot = st.next_slot++;

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

        Block b;
        std::memset(&b, 0, sizeof(b));
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
            fprintf(stderr, "Out of scratch registers\n");
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
            printf("[BASIC] FOR %s = (%s) TO (%s) STEP (%s)\n", var_name.data(), init_expr.data(),
                   limit_expr.data(), step_expr.data());
        }
        return 0;
    }

    // NEXT
    if (blackbox::tools::starts_with_ci(s, "NEXT")) {
        const char* arg = skip_ws(s + 4);
        Block* top = bstack_peek();
        if (!top || top->kind != BLOCK_FOR) {
            fprintf(stderr, "Error line %d: NEXT without FOR\n", lineno);
            return 1;
        }
        if (*arg) {
            std::string next_var;
            const char* next_end = arg;
            if (!parse_identifier(arg, &next_end, next_var)) {
                fprintf(stderr, "Syntax error line %d: expected NEXT [<identifier>]\n", lineno);
                return 1;
            }
            if (*skip_ws(next_end) != '\0') {
                fprintf(stderr, "Syntax error line %d: expected NEXT [<identifier>]\n", lineno);
                return 1;
            }
            if (!blackbox::tools::equals_ci(next_var.data(), top->for_var_name)) {
                fprintf(stderr,
                        "Error line %d: NEXT variable '%s' does not match FOR variable '%s'\n",
                        lineno, next_var.data(), top->for_var_name);
                return 1;
            }
        }
        Block b = bstack_pop();
        RegGuard var_r(&ra), step_r(&ra);
        if (!var_r.ok() || !step_r.ok()) {
            fprintf(stderr, "Out of scratch registers\n");
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
            printf("[BASIC] NEXT %s\n", b.for_var_name);
        }
        return 0;
    }

    // INC
    if (blackbox::tools::starts_with_ci(s, "INC")) {
        const char* name = skip_ws(s + 4);
        Variable* v = sym_find(name);
        if (!v) {
            fprintf(stderr, "Undefined variable '%s' on line %d\n", name, lineno);
            return 1;
        }
        if (v->is_const) {
            fprintf(stderr, "Error line %d: cannot INC CONST '%s'\n", lineno, name);
            return 1;
        }
        if (v->type != VAR_INT) {
            fprintf(stderr, "Type error line %d: cannot INC non-integer '%s'\n", lineno, name);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers on line %d\n", lineno);
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
            fprintf(stderr, "Undefined variable '%s' on line %d\n", name, lineno);
            return 1;
        }
        if (v->is_const) {
            fprintf(stderr, "Error line %d: cannot DEC CONST '%s'\n", lineno, name);
            return 1;
        }
        if (v->type != VAR_INT) {
            fprintf(stderr, "Type error line %d: cannot DEC non-integer '%s'\n", lineno, name);
            return 1;
        }
        RegGuard rg(&ra);
        if (!rg.ok()) {
            fprintf(stderr, "Out of scratch registers on line %d\n", lineno);
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
        for (int i = (int) bs.items.size() - 1; i >= 0; i--) {
            if (bs.items[i].kind == BLOCK_WHILE || bs.items[i].kind == BLOCK_FOR) {
                target = &bs.items[i];
                break;
            }
        }
        if (!target) {
            fprintf(stderr, "Error line %d: BREAK outside WHILE or FOR loop\n", lineno);
            return 1;
        }
        EMIT_CODE(this, "    JMP %s", target->end_label + 1);
        if (debug) {
            printf("[BASIC] BREAK\n");
        }
        return 0;
    }

    // CONTINUE
    if (blackbox::tools::equals_ci(s, "CONTINUE")) {
        Block* target = nullptr;
        for (int i = (int) bs.items.size() - 1; i >= 0; i--) {
            if (bs.items[i].kind == BLOCK_WHILE || bs.items[i].kind == BLOCK_FOR) {
                target = &bs.items[i];
                break;
            }
        }
        if (!target) {
            fprintf(stderr, "Error line %d: CONTINUE outside WHILE or FOR loop\n", lineno);
            return 1;
        }
        if (target->kind == BLOCK_WHILE) {
            EMIT_CODE(this, "    JMP %s", target->loop_label + 1);
            if (debug) {
                printf("[BASIC] CONTINUE (WHILE)\n");
            }
        } else {
            RegGuard var_r(&ra), step_r(&ra);
            if (!var_r.ok() || !step_r.ok()) {
                fprintf(stderr, "Out of scratch registers\n");
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
                printf("[BASIC] CONTINUE (FOR %s)\n", target->for_var_name);
            }
        }
        return 0;
    }

    // CLRSCR
    if (blackbox::tools::equals_ci(s, "CLRSCR")) {
        EMIT_CODE(this, "    CLRSCR");
        if (debug) {
            printf("[BASIC] CLRSCR\n");
        }
        return 0;
    }

    fprintf(stderr, "Unknown statement on line %d: %s\n", lineno, s);
    return 1;
}

int preprocess_basic(const char* input_file, const char* output_file, int debug) {
    FILE* in = fopen(input_file, "r");
    if (!in) {
        perror("fopen input");
        return 1;
    }

    CompilerState cs;
    cs.debug = debug;

    bool in_asm_block = false;
    int result = 0;
    char line[8192];

    while (fgets(line, sizeof(line), in)) {
        cs.lineno++;
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
        cs.set_emit_context(s);

        if (blackbox::tools::equals_ci(s, "ASM:")) {
            in_asm_block = true;
            continue;
        }
        if (blackbox::tools::equals_ci(s, "ENDASM")) {
            in_asm_block = false;
            continue;
        }

        if (in_asm_block) {
            EMIT_CODE_META(&cs, "inline ASM block", "%s", s);
            continue;
        }

        if (cs.compile_line(s)) {
            result = 1;
            break;
        }
    }

    fclose(in);

    if (!cs.bs.items.empty() && result == 0) {
        fprintf(stderr, "Error: unclosed block (%s)\n",
                cs.bs.items.back().kind == BLOCK_IF      ? "IF"
                : cs.bs.items.back().kind == BLOCK_WHILE ? "WHILE"
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

    fprintf(out, "%%asm\n");
    if (!cs.ob.data_sec.empty()) {
        fprintf(out, "%%data\n");
        fwrite(cs.ob.data_sec.data(), 1, cs.ob.data_sec.size(), out);
    }
    fprintf(out, "%%main\n");
    fprintf(out, "    CALL __bbx_basic_main\n");
    fprintf(out, "    HALT OK\n");
    fprintf(out, ".__bbx_basic_main:\n");
    if (cs.st.next_slot > 0) {
        fprintf(out, "    FRAME %u\n", cs.st.next_slot);
    }
    fwrite(cs.ob.code_sec.data(), 1, cs.ob.code_sec.size(), out);
    fprintf(out, "    RET\n");
    fclose(out);

    if (debug) {
        printf("[BASIC] Emitted %zu data bytes, %zu code bytes, %u slots\n", cs.ob.data_sec.size(),
               cs.ob.code_sec.size(), cs.st.next_slot);
    }
    return 0;
}