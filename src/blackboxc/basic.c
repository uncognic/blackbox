#include "basic.h"
#include "tools.h"
#include "../define.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

static char g_emit_ctx[512];

static const char *block_kind_name(BlockKind kind)
{
    switch (kind)
    {
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

static void set_emit_context(int lineno, const char *stmt, const BlockStack *bs)
{
    if (!stmt)
        stmt = "";

    char snippet[192];
    size_t n = strlen(stmt);
    if (n >= sizeof(snippet))
        n = sizeof(snippet) - 1;
    memcpy(snippet, stmt, n);
    snippet[n] = '\0';

    if (bs && bs->top > 0)
    {
        Block *top = (Block *)&bs->items[bs->top - 1];
        if (top->kind == BLOCK_FOR && top->for_var_name[0] != '\0')
        {
            snprintf(g_emit_ctx, sizeof(g_emit_ctx),
                     "BASIC line %d | block=%s(%s) | src=%s",
                     lineno, block_kind_name(top->kind), top->for_var_name, snippet);
        }
        else
        {
            snprintf(g_emit_ctx, sizeof(g_emit_ctx),
                     "BASIC line %d | block=%s | src=%s",
                     lineno, block_kind_name(top->kind), snippet);
        }
        return;
    }

    snprintf(g_emit_ctx, sizeof(g_emit_ctx),
             "BASIC line %d | src=%s", lineno, snippet);
}

static int ralloc_acquire(RegAlloc *ra)
{
    for (int i = 0; i < SCRATCH_COUNT; i++)
    {
        if (!(ra->used & (1u << i)))
        {
            ra->used |= (1u << i);
            return SCRATCH_MIN + i;
        }
    }
    return -1;
}

static void ralloc_release(RegAlloc *ra, int reg)
{
    if (reg < SCRATCH_MIN || reg > SCRATCH_MAX)
        return;
    ra->used &= ~(1u << (reg - SCRATCH_MIN));
}

static void reg_name(int reg, char *buf)
{
    snprintf(buf, 4, "R%02d", reg);
}

static void sym_init(SymbolTable *symtab)
{
    symtab->vars = NULL;
    symtab->count = 0;
    symtab->cap = 0;
    symtab->next_slot = 0;
    symtab->next_data_id = 0;
}
static void sym_free(SymbolTable *symtab)
{
    free(symtab->vars);
    symtab->vars = NULL;
    symtab->count = 0;
    symtab->cap = 0;
    symtab->next_slot = 0;
    symtab->next_data_id = 0;
}
static Variable *sym_find(SymbolTable *symtab, const char *name)
{
    for (size_t i = 0; i < symtab->count; i++)
    {
        if (strcmp(symtab->vars[i].name, name) == 0)
            return &symtab->vars[i];
    }
    return NULL;
}
static Variable *sym_add_int(SymbolTable *symtab, const char *name)
{
    if (symtab->count + 1 >= symtab->cap)
    {
        size_t new_cap = symtab->cap ? symtab->cap * 2 : 16;
        Variable *nv = realloc(symtab->vars, new_cap * sizeof(Variable));
        if (!nv)
        {
            return NULL;
        }
        symtab->vars = nv;
        symtab->cap = new_cap;
    }
    Variable *var = &symtab->vars[symtab->count++];
    memset(var, 0, sizeof(*var));
    strncpy(var->name, name, sizeof(var->name) - 1);
    var->type = VAR_INT;
    var->slot = symtab->next_slot++;
    return var;
}
static Variable *sym_add_str(SymbolTable *symtab, const char *name, const char *data_name, int is_const)
{
    if (symtab->count + 1 >= symtab->cap)
    {
        size_t new_cap = symtab->cap ? symtab->cap * 2 : 16;
        Variable *nv = realloc(symtab->vars, new_cap * sizeof(Variable));
        if (!nv)
        {
            return NULL;
        }
        symtab->vars = nv;
        symtab->cap = new_cap;
    }
    Variable *var = &symtab->vars[symtab->count++];
    memset(var, 0, sizeof(*var));
    strncpy(var->name, name, sizeof(var->name) - 1);
    var->type = VAR_STR;
    var->is_const = is_const;
    if (!is_const)
        var->slot = symtab->next_slot++;
    strncpy(var->data_name, data_name, sizeof(var->data_name) - 1);
    return var;
}

static void outbuf_init(OutBuf *out)
{
    out->data_sec = NULL;
    out->data_len = 0;
    out->data_cap = 0;

    out->code_sec = NULL;
    out->code_len = 0;
    out->code_cap = 0;
}
static void outbuf_free(OutBuf *out)
{
    free(out->data_sec);
    out->data_sec = NULL;
    out->data_len = 0;
    out->data_cap = 0;

    free(out->code_sec);
    out->code_sec = NULL;
    out->code_len = 0;
    out->code_cap = 0;
}
static int outbuf_append(char **buf, size_t *len, size_t *cap, const char *txt)
{
    size_t txtlen = strlen(txt);
    if (*len + txtlen + 1 > *cap)
    {
        size_t new_cap = (*cap) ? (*cap * 2) : 4096;
        while (new_cap < *len + txtlen + 2)
            new_cap *= 2;
        char *nb = realloc(*buf, new_cap);
        if (!nb)
            return 1;
        *buf = nb;
        *cap = new_cap;
    }
    memcpy(*buf + *len, txt, txtlen);
    *len += txtlen;
    (*buf)[*len] = '\0';
    return 0;
}

#define EMIT_DATA(ob, fmt, ...)                                                     \
    do                                                                              \
    {                                                                               \
        char _tmp[512];                                                             \
        snprintf(_tmp, sizeof(_tmp), fmt "\n", ##__VA_ARGS__);                      \
        if (outbuf_append(&(ob)->data_sec, &(ob)->data_len, &(ob)->data_cap, _tmp)) \
            return 1;                                                               \
    } while (0)

static int emit_code_comment(OutBuf *ob, const char *detail, const char *fmt, ...)
{
    char ins[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ins, sizeof(ins), fmt, ap);
    va_end(ap);

    char line[1024];
    if (detail && *detail)
        snprintf(line, sizeof(line), "%s ; %s | %s\n\n", ins, g_emit_ctx[0] ? g_emit_ctx : "BASIC", detail);
    else
        snprintf(line, sizeof(line), "%s ; %s\n\n", ins, g_emit_ctx[0] ? g_emit_ctx : "BASIC");

    if (outbuf_append(&ob->code_sec, &ob->code_len, &ob->code_cap, line))
        return 1;
    return 0;
}

#define EMIT_CODE(ob, fmt, ...)                                \
    do                                                         \
    {                                                          \
        if (emit_code_comment((ob), NULL, fmt, ##__VA_ARGS__)) \
            return 1;                                          \
    } while (0)

#define EMIT_CODE_META(ob, meta, fmt, ...)                       \
    do                                                           \
    {                                                            \
        if (emit_code_comment((ob), (meta), fmt, ##__VA_ARGS__)) \
            return 1;                                            \
    } while (0)

static int emit_expr(const char *s, SymbolTable *st, RegAlloc *ra, OutBuf *ob, int debug, int *out_reg);

static const char *skip_ws(const char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

static const char *find_keyword_token(const char *s, const char *kw)
{
    size_t n = strlen(kw);
    for (const char *p = s; *p; p++)
    {
        if (!starts_with_ci(p, kw))
            continue;

        int left_ok = (p == s) || !(isalnum((unsigned char)p[-1]) || p[-1] == '_');
        int right_ok = !(isalnum((unsigned char)p[n]) || p[n] == '_');
        if (left_ok && right_ok)
            return p;
    }
    return NULL;
}

static char *dup_trim_range(const char *start, const char *end)
{
    while (start < end && isspace((unsigned char)*start))
        start++;
    while (end > start && isspace((unsigned char)*(end - 1)))
        end--;

    size_t len = (size_t)(end - start);
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;

    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static int emit_atom(const char *s, const char **end, SymbolTable *st, RegAlloc *ra, OutBuf *ob, int debug, int *out_reg)
{
    s = skip_ws(s);

    // parnthesized sub-expr
    if (*s == '(')
    {
        s++;

        int res = emit_expr(s, st, ra, ob, debug, out_reg);
        if (res != 0)
            return res;

        s = skip_ws(*end); // emit_expr sets *end

        if (*s != ')')
        {
            fprintf(stderr, "Expected ')' in expression\n");
            return 1;
        }
        s++;
        *end = s;
        return 0;
    }

    // integer literal
    if (*s == '-' || isdigit((unsigned char)*s))
    {
        char *endptr;
        int32_t val = (int32_t)strtol(s, &endptr, 10);
        int reg = ralloc_acquire(ra);
        if (reg < 0)
        {
            fprintf(stderr, "Out of registers\n");
            return 1;
        }
        char rn[4];
        reg_name(reg, rn);
        EMIT_CODE(ob, "    MOVI %s, %d", rn, val);
        *out_reg = reg;
        *end = endptr;
        return 0;
    }

    if (isalpha((unsigned char)*s) || *s == '_')
    {
        char name[64];
        size_t i = 0;
        while ((isalnum((unsigned char)*s) || *s == '_') && i < sizeof(name) - 1)
            name[i++] = *s++;
        name[i] = '\0';
        *end = s;

        Variable *v = sym_find(st, name);
        if (!v)
        {
            fprintf(stderr, "Undefined variable '%s'\n", name);
            return 1;
        }
        int reg = ralloc_acquire(ra);
        if (reg < 0)
        {
            fprintf(stderr, "Out of scratch registers\n");
            return 1;
        }
        char rn[4];
        reg_name(reg, rn);

        if (v->type == VAR_INT || !v->is_const)
            EMIT_CODE_META(ob, v->name, "    LOADVAR %s, %u", rn, v->slot);
        else
            EMIT_CODE(ob, "    LOADSTR $%s, %s", v->data_name, rn);
        *out_reg = reg;
        return 0;
    }

    fprintf(stderr, "Expression error: unexpected character '%c'\n", *s);
    return 1;
}

static int emit_mul(const char *s, const char **end,
                    SymbolTable *st, RegAlloc *ra, OutBuf *ob, int debug,
                    int *out_reg)
{
    int lreg;
    if (emit_atom(s, end, st, ra, ob, debug, &lreg))
        return 1;
    s = skip_ws(*end);

    while (*s == '*' || *s == '/' || *s == '%')
    {
        char op = *s++;
        s = skip_ws(s);
        int rreg;
        if (emit_atom(s, end, st, ra, ob, debug, &rreg))
        {
            ralloc_release(ra, lreg);
            return 1;
        }
        s = skip_ws(*end);

        char ln[4], rn[4];
        reg_name(lreg, ln);
        reg_name(rreg, rn);

        if (op == '*')
            EMIT_CODE(ob, "    MUL %s, %s", ln, rn);
        else if (op == '/')
            EMIT_CODE(ob, "    DIV %s, %s", ln, rn);
        else
            EMIT_CODE(ob, "    MOD %s, %s", ln, rn);

        ralloc_release(ra, rreg);
    }
    *end = s;
    *out_reg = lreg;
    return 0;
}

static int emit_expr(const char *s, SymbolTable *st, RegAlloc *ra,
                     OutBuf *ob, int debug, int *out_reg)
{
    const char *p = s;
    int lreg;
    if (emit_mul(p, &p, st, ra, ob, debug, &lreg))
        return 1;
    p = skip_ws(p);

    while (*p == '+' || *p == '-')
    {
        char op = *p++;
        p = skip_ws(p);
        int rreg;
        if (emit_mul(p, &p, st, ra, ob, debug, &rreg))
        {
            ralloc_release(ra, lreg);
            return 1;
        }
        p = skip_ws(p);

        char ln[4], rn[4];
        reg_name(lreg, ln);
        reg_name(rreg, rn);

        if (op == '+')
            EMIT_CODE(ob, "    ADD %s, %s", ln, rn);
        else
            EMIT_CODE(ob, "    SUB %s, %s", ln, rn);

        ralloc_release(ra, rreg);
    }
    *out_reg = lreg;
    (void)p;
    return 0;
}

static int emit_expr_p(const char *s, const char **end,
                       SymbolTable *st, RegAlloc *ra, OutBuf *ob, int debug,
                       int *out_reg)
{
    const char *p = s;
    int lreg;
    if (emit_mul(p, &p, st, ra, ob, debug, &lreg))
        return 1;
    p = skip_ws(p);

    while (*p == '+' || *p == '-')
    {
        char op = *p++;
        p = skip_ws(p);
        int rreg;
        if (emit_mul(p, &p, st, ra, ob, debug, &rreg))
        {
            ralloc_release(ra, lreg);
            return 1;
        }
        p = skip_ws(p);

        char ln[4], rn[4];
        reg_name(lreg, ln);
        reg_name(rreg, rn);

        if (op == '+')
            EMIT_CODE(ob, "    ADD %s, %s", ln, rn);
        else
            EMIT_CODE(ob, "    SUB %s, %s", ln, rn);

        ralloc_release(ra, rreg);
    }
    *end = p;
    *out_reg = lreg;
    return 0;
}

static int emit_condition(const char *s, SymbolTable *st, RegAlloc *ra,
                          OutBuf *ob, int debug,
                          const char *skip_label,
                          unsigned long *uid)
{
    const char *p = s;

    for (const char *q = p; *q; q++)
    {
        if (starts_with_ci(q, "OR") &&
            (q == p || !(isalnum((unsigned char)q[-1]) || q[-1] == '_')) &&
            !(isalnum((unsigned char)q[2]) || q[2] == '_'))
        {
            size_t left_len = (size_t)(q - p);
            char *left = (char *)malloc(left_len + 1);
            if (!left)
            {
                fprintf(stderr, "Out of memory while parsing condition\n");
                return 1;
            }
            memcpy(left, p, left_len);
            left[left_len] = '\0';

            char next_or_label[64];
            char pass_label[64];
            snprintf(next_or_label, sizeof(next_or_label), "_or_next_%lu", (*uid)++);
            snprintf(pass_label, sizeof(pass_label), "_or_pass_%lu", (*uid)++);

            if (emit_condition(left, st, ra, ob, debug, next_or_label, uid))
            {
                free(left);
                return 1;
            }
            free(left);

            EMIT_CODE(ob, "    JMP %s", pass_label);
            EMIT_CODE(ob, ".%s:", next_or_label);

            if (emit_condition(skip_ws(q + 2), st, ra, ob, debug, skip_label, uid))
                return 1;

            EMIT_CODE(ob, ".%s:", pass_label);
            return 0;
        }
    }

    while (1)
    {
        int lreg;
        if (emit_expr_p(p, &p, st, ra, ob, debug, &lreg))
            return 1;
        p = skip_ws(p);

        char op[3] = {0};
        if (strncmp(p, "==", 2) == 0)
        {
            op[0] = '=';
            op[1] = '=';
            p += 2;
        }
        else if (strncmp(p, "!=", 2) == 0)
        {
            op[0] = '!';
            op[1] = '=';
            p += 2;
        }
        else if (strncmp(p, "<=", 2) == 0)
        {
            op[0] = '<';
            op[1] = '=';
            p += 2;
        }
        else if (strncmp(p, ">=", 2) == 0)
        {
            op[0] = '>';
            op[1] = '=';
            p += 2;
        }
        else if (*p == '=')
        {
            op[0] = '=';
            op[1] = '=';
            p++;
        }
        else if (*p == '<')
        {
            op[0] = '<';
            p++;
        }
        else if (*p == '>')
        {
            op[0] = '>';
            p++;
        }
        else
        {
            fprintf(stderr, "Condition error: expected comparison operator near '%s'\n", p);
            ralloc_release(ra, lreg);
            return 1;
        }
        p = skip_ws(p);

        int rreg;
        if (emit_expr_p(p, &p, st, ra, ob, debug, &rreg))
        {
            ralloc_release(ra, lreg);
            return 1;
        }

        char ln[4], rn[4];
        reg_name(lreg, ln);
        reg_name(rreg, rn);

        const char *next = skip_ws(p);
        int is_and = starts_with_ci(next, "AND") &&
                     !(isalnum((unsigned char)next[3]) || next[3] == '_');

        EMIT_CODE(ob, "    CMP %s, %s", strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ? rn : ln,
                  strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ? ln : rn);

        if (strcmp(op, "==") == 0)
            EMIT_CODE(ob, "    JNE %s", skip_label);
        else if (strcmp(op, "!=") == 0)
            EMIT_CODE(ob, "    JE  %s", skip_label);
        else if (strcmp(op, "<") == 0)
            EMIT_CODE(ob, "    JGE %s", skip_label);
        else if (strcmp(op, ">=") == 0)
            EMIT_CODE(ob, "    JL  %s", skip_label);
        else if (strcmp(op, ">") == 0)
            EMIT_CODE(ob, "    JGE %s", skip_label);
        else if (strcmp(op, "<=") == 0)
            EMIT_CODE(ob, "    JL  %s", skip_label);

        ralloc_release(ra, lreg);
        ralloc_release(ra, rreg);

        if (is_and)
        {
            p = skip_ws(next + 3);
            continue;
        }

        break;
    }

    return 0;
}

static int emit_write_values(const char *arg,
                             SymbolTable *st, RegAlloc *ra, OutBuf *ob,
                             int debug, int lineno,
                             unsigned long *uid,
                             const char *stmt_name)
{
    while (*arg)
    {
        if (*arg == '"')
        {
            const char *str_start = arg + 1;
            const char *str_end = strchr(str_start, '"');
            if (!str_end)
            {
                fprintf(stderr, "Syntax error line %d: unterminated string in %s\n", lineno, stmt_name);
                return 1;
            }

            char data_name[64];
            snprintf(data_name, sizeof(data_name), "_p%lu", (*uid)++);

            EMIT_DATA(ob, "    STR $%s, \"%.*s\"",
                      data_name, (int)(str_end - str_start), str_start);

            int reg = ralloc_acquire(ra);
            if (reg < 0)
            {
                fprintf(stderr, "Out of scratch registers\n");
                return 1;
            }

            char rn[4];
            reg_name(reg, rn);

            EMIT_CODE(ob, "    LOADSTR $%s, %s", data_name, rn);
            EMIT_CODE(ob, "    PRINTSTR %s", rn);
            ralloc_release(ra, reg);

            if (debug)
                printf("[BASIC] %s \"%.*s\"\n", stmt_name, (int)(str_end - str_start), str_start);

            arg = skip_ws(str_end + 1);
        }
        else
        {
            const char *expr_end = NULL;
            int reg = 0;

            if (emit_expr_p(arg, &expr_end, st, ra, ob, debug, &reg))
                return 1;

            char rn[4];
            reg_name(reg, rn);
            EMIT_CODE(ob, "    PRINTREG %s", rn);
            ralloc_release(ra, reg);

            if (debug)
                printf("[BASIC] %s %.*s\n", stmt_name, (int)(expr_end - arg), arg);

            arg = skip_ws(expr_end);
        }

        if (*arg == ',')
        {
            arg = skip_ws(arg + 1);
            if (*arg == '\0')
            {
                fprintf(stderr, "Syntax error line %d: expected value after ',' in %s\n", lineno, stmt_name);
                return 1;
            }
            continue;
        }

        if (*arg != '\0')
        {
            fprintf(stderr, "Syntax error line %d: expected ',' between %s values\n", lineno, stmt_name);
            return 1;
        }
    }

    return 0;
}

static void bstack_push(BlockStack *bs, Block b)
{
    bs->items[bs->top++] = b;
}
static Block *bstack_peek(BlockStack *bs)
{
    return bs->top ? &bs->items[bs->top - 1] : NULL;
}
static Block bstack_pop(BlockStack *bs)
{
    return bs->items[--bs->top];
}

// BASIC entry point
int preprocess_basic(const char *input_file, const char *output_file, int debug)
{
    FILE *in = fopen(input_file, "r");
    if (!in)
    {
        perror("fopen input");
        return 1;
    }

    SymbolTable st;
    sym_init(&st);

    OutBuf ob;
    outbuf_init(&ob);

    RegAlloc ra;
    ra.used = 0;

    BlockStack bs;
    bs.top = 0;

    unsigned long uid = 0; // label counter for unique labels
    bool in_asm_block = 0;
    int lineno = 0;
    int result = 0;

    // we need to know the total slot count before we ALLOC.
    // so first pass: parse and collect variables, but don't emit code
    char line[8192];
    while (fgets(line, sizeof(line), in))
    {
        lineno++;
        char *s = trim(line);

        char *comment = strstr(s, "//");
        if (comment)
        {
            *comment = '\0';
            s = trim(s);
        }

        if (*s == '\0')
            continue;

        set_emit_context(lineno, s, &bs);

        if (equals_ci(s, "ASM:"))
        {
            in_asm_block = true;
            continue;
        }

        if (equals_ci(s, "ENDASM"))
        {
            in_asm_block = false;
            continue;
        }

        if (in_asm_block)
        {
            EMIT_CODE_META(&ob, "inline ASM block", "%s", s);
            continue;
        }

        // CONST name = value/"string"
        if (starts_with_ci(s, "CONST "))
        {
            char name[64];
            char *eq = strchr(s + 6, '=');

            if (!eq)
            {
                fprintf(stderr, "Syntax error line %d: expected CONST <name> = <value>\n", lineno);
                fprintf(stderr, "Got: %s\n", s);
                result = 1;
                break;
            }

            char lhs[128];
            size_t lhslen = (size_t)(eq - (s + 6));

            if (lhslen >= sizeof(lhs))
            {
                lhslen = sizeof(lhs) - 1; // truncate if too long
            }

            memcpy(lhs, s + 6, lhslen);
            lhs[lhslen] = '\0';
            strncpy(name, trim(lhs), sizeof(name) - 1);

            if (sym_find(&st, name))
            {
                fprintf(stderr, "Error line %d: variable '%s' already defined\n", lineno, name);
                result = 1;
                break;
            }

            char *rhs = trim(eq + 1);

            if (rhs[0] == '"')
            {
                char *str_start = rhs + 1;
                char *str_end = strchr(str_start, '"');
                if (!str_end)
                {
                    fprintf(stderr, "Syntax error line %d: unterminated string literal\n", lineno);
                    fprintf(stderr, "Got: %s\n", s);
                    result = 1;
                    break;
                }

                // generate unique data label
                char data_name[64];
                snprintf(data_name, sizeof(data_name), "_s%lu_%s", uid++, name);

                EMIT_DATA(&ob, "    STR $%s, \"%.*s\"",
                          data_name, (int)(str_end - str_start), str_start);

                Variable *v = sym_add_str(&st, name, data_name, 1);
                if (!v)
                {
                    fprintf(stderr, "Out of memory\n");
                    result = 1;
                    break;
                }
                if (debug)
                    printf("[BASIC] CONST string %s -> $%s\n", name, data_name);
            }
            else
            {
                Variable *v = sym_add_int(&st, name);
                if (!v)
                {
                    fprintf(stderr, "Out of memory\n");
                    result = 1;
                    break;
                }
                v->is_const = 1;

                int reg = ralloc_acquire(&ra);
                char rn[4];
                reg_name(reg, rn);

                int ereg;
                if (emit_expr(rhs, &st, &ra, &ob, debug, &ereg))
                {
                    result = 1;
                    break;
                }
                char ern[4];
                reg_name(ereg, ern);

                EMIT_CODE_META(&ob, name, "    STOREVAR %s, %u", ern, v->slot);
                ralloc_release(&ra, ereg);
                (void)reg;
                ralloc_release(&ra, reg);

                if (debug)
                    printf("[BASIC] CONST int %s -> slot %u\n", name, v->slot);
            }
            continue;
        }

        // VAR name = value
        if (starts_with_ci(s, "VAR "))
        {
            char *eq = strchr(s + 4, '=');
            if (!eq)
            {
                fprintf(stderr, "Syntax error line %d: expected VAR <name> = <value>\n", lineno);
                fprintf(stderr, "Got: %s\n", s);
                result = 1;
                break;
            }

            char lhs[64];
            size_t lhslen = (size_t)(eq - (s + 4));
            if (lhslen >= sizeof(lhs))
            {
                lhslen = sizeof(lhs) - 1; // truncate if too long
            }
            memcpy(lhs, s + 4, lhslen);
            lhs[lhslen] = '\0';
            char *name = trim(lhs);

            char *rhs = trim(eq + 1);

            if (*rhs == '"')
            {
                char *str_start = rhs + 1;
                char *str_end = strchr(str_start, '"');
                if (!str_end)
                {
                    fprintf(stderr, "Syntax error line %d: unterminated string\n", lineno);
                    result = 1;
                    break;
                }
                char data_name[64];
                snprintf(data_name, sizeof(data_name), "_s%lu_%s", uid++, name);
                EMIT_DATA(&ob, "    STR $%s, \"%.*s\"",
                          data_name, (int)(str_end - str_start), str_start);
                Variable *v = sym_add_str(&st, name, data_name, 0);
                int reg = ralloc_acquire(&ra);
                char rn[4];
                reg_name(reg, rn);
                EMIT_CODE(&ob, "    LOADSTR $%s, %s", data_name, rn);
                EMIT_CODE_META(&ob, name, "    STOREVAR %s, %u", rn, v->slot);
                ralloc_release(&ra, reg);
                if (debug)
                    printf("[BASIC] VAR string %s -> $%s\n", name, data_name);
            }
            else
            {
                Variable *v = sym_find(&st, name);
                if (v)
                {
                    fprintf(stderr, "Error line %d: variable '%s' already defined\n", lineno, name);
                    result = 1;
                    break;
                }
                v = sym_add_int(&st, name);
                if (!v)
                {
                    fprintf(stderr, "Out of memory\n");
                    result = 1;
                    break;
                }
                int ereg;
                if (emit_expr(rhs, &st, &ra, &ob, debug, &ereg))
                {
                    result = 1;
                    break;
                }
                char ern[4];
                reg_name(ereg, ern);
                EMIT_CODE_META(&ob, name, "    STOREVAR %s, %u", ern, v->slot);
                ralloc_release(&ra, ereg);
                if (debug)
                {
                    printf("[BASIC] VAR int %s -> slot %u\n", name, v->slot);
                }
            }
            continue;
        }
        // name = expr (assignment)
        {
            char *eq = strchr(s, '=');
            for (char *p = s; *p; p++)
            {
                if (*p == '=' && *(p + 1) != '=' &&
                    (p == s || (*(p - 1) != '!' && *(p - 1) != '<' && *(p - 1) != '>')))
                {
                    eq = p;
                    break;
                }
            }
            if (eq)
            {
                char lhs[64];
                size_t lhslen = (size_t)(eq - s);
                if (lhslen < sizeof(lhs))
                {
                    memcpy(lhs, s, lhslen);
                    lhs[lhslen] = '\0';
                    char *name = trim(lhs);
                    Variable *v = sym_find(&st, name);
                    if (v)
                    {
                        if (v->is_const)
                        {
                            fprintf(stderr,
                                    "Error line %d: cannot assign to CONST '%s'\n",
                                    lineno, name);
                            result = 1;
                            break;
                        }

                        char *rhs = trim(eq + 1);

                        if (v->type == VAR_STR)
                        {
                            if (*rhs != '"')
                            {
                                fprintf(stderr,
                                        "Type error line %d: expected string literal for '%s'\n",
                                        lineno, name);
                                result = 1;
                                break;
                            }

                            char *str_start = rhs + 1;
                            char *str_end = strchr(str_start, '"');
                            if (!str_end)
                            {
                                fprintf(stderr, "Syntax error line %d: unterminated string\n", lineno);
                                result = 1;
                                break;
                            }

                            if (*skip_ws(str_end + 1) != '\0')
                            {
                                fprintf(stderr,
                                        "Syntax error line %d: unexpected trailing tokens after string literal\n",
                                        lineno);
                                result = 1;
                                break;
                            }

                            char data_name[64];
                            snprintf(data_name, sizeof(data_name), "_s%lu_%s", uid++, name);

                            EMIT_DATA(&ob, "    STR $%s, \"%.*s\"",
                                      data_name, (int)(str_end - str_start), str_start);

                            strncpy(v->data_name, data_name, sizeof(v->data_name) - 1);
                            v->data_name[sizeof(v->data_name) - 1] = '\0';

                            int reg = ralloc_acquire(&ra);
                            char rn[4];
                            reg_name(reg, rn);
                            EMIT_CODE(&ob, "    LOADSTR $%s, %s", data_name, rn);
                            EMIT_CODE_META(&ob, name, "    STOREVAR %s, %u", rn, v->slot);
                            ralloc_release(&ra, reg);

                            if (debug)
                                printf("[BASIC] ASSIGN string %s -> $%s\n", name, data_name);
                            continue;
                        }

                        if (v->type != VAR_INT)
                        {
                            fprintf(stderr,
                                    "Type error line %d: cannot assign numeric expression to string '%s'\n",
                                    lineno, name);
                            result = 1;
                            break;
                        }

                        int ereg;
                        if (emit_expr(rhs, &st, &ra, &ob, debug, &ereg))
                        {
                            result = 1;
                            break;
                        }
                        char ern[4];
                        reg_name(ereg, ern);
                        EMIT_CODE_META(&ob, name, "    STOREVAR %s, %u", ern, v->slot);
                        ralloc_release(&ra, ereg);
                        if (debug)
                            printf("[BASIC] ASSIGN %s -> slot %u\n", name, v->slot);
                        continue;
                    }
                }
            }
        }

        // IF condition: ... ENDIF
        if (starts_with_ci(s, "IF "))
        {
            size_t slen = strlen(s);
            if (s[slen - 1] == ':')
            { // strip :
                s[slen - 1] = '\0';
            }

            char end_label[64];
            char else_label[64];

            snprintf(end_label, sizeof(end_label), "endif_%lu", uid);
            snprintf(else_label, sizeof(else_label), "else_%lu", uid);
            uid++;

            Block b;
            b.kind = BLOCK_IF;
            b.has_else = 0;

            snprintf(b.end_label, sizeof(b.end_label), ".%s", end_label);
            snprintf(b.else_label, sizeof(b.else_label), ".%s", else_label);
            b.loop_label[0] = '\0';

            if (emit_condition(s + 3, &st, &ra, &ob, debug, b.else_label + 1, &uid))
            {
                result = 1;
                break;
            }

            bstack_push(&bs, b);
            if (debug)
                printf("[BASIC] IF condition, skip to %s if false\n", b.else_label);
            continue;
        }

        // ELSE IF condition (treated as ELSE { IF ... })
        if (starts_with_ci(s, "ELSE "))
        {
            const char *p = s + 5;
            p = skip_ws(p);

            if (starts_with_ci(p, "IF "))
            {
                Block b = bstack_pop(&bs);
                if (b.kind != BLOCK_IF)
                {
                    fprintf(stderr, "Error line %d: ELSE IF without IF\n", lineno);
                    result = 1;
                    break;
                }

                EMIT_CODE(&ob, "    JMP %s", b.end_label + 1);
                EMIT_CODE(&ob, "%s:", b.else_label);

                const char *if_start = p + 3;
                size_t if_len = strlen(if_start);
                if (if_len > 0 && if_start[if_len - 1] == ':')
                    if_len--;

                char *if_stmt = (char *)malloc(if_len + 1);
                if (!if_stmt)
                {
                    fprintf(stderr, "Out of memory parsing ELSE IF\n");
                    result = 1;
                    break;
                }
                memcpy(if_stmt, if_start, if_len);
                if_stmt[if_len] = '\0';

                char new_else_label[64];
                snprintf(new_else_label, sizeof(new_else_label), "else_%lu", uid++);

                if (emit_condition(if_stmt, &st, &ra, &ob, debug, new_else_label, &uid))
                {
                    free(if_stmt);
                    result = 1;
                    break;
                }
                free(if_stmt);

                Block new_b;
                new_b.kind = BLOCK_IF;
                new_b.has_else = 0;
                snprintf(new_b.end_label, sizeof(new_b.end_label), "%s", b.end_label);
                snprintf(new_b.else_label, sizeof(new_b.else_label), ".%s", new_else_label);
                new_b.loop_label[0] = '\0';

                bstack_push(&bs, new_b);
                if (debug)
                    printf("[BASIC] ELSE IF condition, else to %s, exit to %s\n", new_b.else_label, new_b.end_label);
                continue;
            }
        }

        // ELSE:
        if (equals_ci(s, "ELSE:"))
        {
            Block *b = bstack_peek(&bs);
            if (!b || b->kind != BLOCK_IF)
            {
                fprintf(stderr, "Error line %d: ELSE without IF\n", lineno);
                result = 1;
                break;
            }
            b->has_else = 1;

            EMIT_CODE(&ob, "    JMP %s", b->end_label + 1);
            EMIT_CODE(&ob, "%s:", b->else_label);
            if (debug)
                printf("[BASIC] ELSE\n");
            continue;
        }

        // ENDIF
        if (equals_ci(s, "ENDIF"))
        {
            Block b = bstack_pop(&bs);
            if (b.kind != BLOCK_IF)
            {
                fprintf(stderr, "Error line %d: ENDIF without IF\n", lineno);
                result = 1;
                break;
            }
            if (!b.has_else)
            {
                EMIT_CODE(&ob, "%s:", b.else_label);
            }
            EMIT_CODE(&ob, "%s:", b.end_label);
            if (debug)
                printf("[BASIC] ENDIF\n");
            continue;
        }
        // WHILE condition: ... ENDWHILE
        if (starts_with_ci(s, "WHILE "))
        {
            size_t slen = strlen(s);
            if (s[slen - 1] == ':')
                s[slen - 1] = '\0';

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

            EMIT_CODE(&ob, "%s:", b.loop_label);

            if (emit_condition(s + 6, &st, &ra, &ob, debug, b.end_label + 1, &uid))
            {
                result = 1;
                break;
            }

            bstack_push(&bs, b);
            if (debug)
                printf("[BASIC] WHILE -> top=%s end=%s\n", b.loop_label, b.end_label);
            continue;
        }

        // ENDWHILE
        if (equals_ci(s, "ENDWHILE"))
        {
            Block b = bstack_pop(&bs);
            if (b.kind != BLOCK_WHILE)
            {
                fprintf(stderr, "Error line %d: ENDWHILE without WHILE\n", lineno);
                result = 1;
                break;
            }
            EMIT_CODE(&ob, "    JMP %s", b.loop_label + 1);
            EMIT_CODE(&ob, "%s:", b.end_label);
            if (debug)
                printf("[BASIC] ENDWHILE\n");
            continue;
        }

        // WRITE (same operands as PRINT, but no trailing newline)
        if (starts_with_ci(s, "WRITE"))
        {
            const char *arg = s + 5;

            if (*arg != '\0' && !isspace((unsigned char)*arg))
            {
                fprintf(stderr, "Syntax error line %d: expected WRITE [<value>[, ...]]\n", lineno);
                result = 1;
                break;
            }

            arg = skip_ws(arg);

            if (*arg != '\0' && emit_write_values(arg, &st, &ra, &ob, debug, lineno, &uid, "WRITE"))
            {
                result = 1;
                break;
            }

            continue;
        }

        // PRINT (WRITE but with a trailing newline)
        if (starts_with_ci(s, "PRINT"))
        {
            const char *arg = s + 5;

            if (*arg != '\0' && !isspace((unsigned char)*arg))
            {
                fprintf(stderr, "Syntax error line %d: expected PRINT [<value>[, ...]]\n", lineno);
                result = 1;
                break;
            }

            arg = skip_ws(arg);

            if (*arg != '\0' && emit_write_values(arg, &st, &ra, &ob, debug, lineno, &uid, "PRINT"))
            {
                result = 1;
                break;
            }

            int nl_reg = ralloc_acquire(&ra);
            if (nl_reg < 0)
            {
                fprintf(stderr, "Out of scratch registers\n");
                result = 1;
                break;
            }

            char nl_rn[4];
            reg_name(nl_reg, nl_rn);
            EMIT_CODE(&ob, "    MOVI %s, 10", nl_rn);
            EMIT_CODE(&ob, "    PRINTCHAR %s", nl_rn);
            ralloc_release(&ra, nl_reg);

            if (debug)
                printf("[BASIC] PRINT <newline>\n");

            continue;
        }

        if (starts_with_ci(s, "HALT"))
        {
            const char *arg = s + 4;

            if (*arg != '\0' && !isspace((unsigned char)*arg))
            {
                fprintf(stderr, "Syntax error line %d: expected HALT [OK|BAD|<number>]\n", lineno);
                result = 1;
                break;
            }

            arg = skip_ws(arg);
            if (*arg == '\0')
            {
                EMIT_CODE(&ob, "    HALT");
                if (debug)
                    printf("[BASIC] HALT\n");
                continue;
            }

            size_t tok_len = 0;
            while (arg[tok_len] && !isspace((unsigned char)arg[tok_len]))
                tok_len++;

            if (tok_len == 0 || tok_len >= 32)
            {
                fprintf(stderr, "Syntax error line %d: invalid HALT operand\n", lineno);
                result = 1;
                break;
            }

            char token[32];
            memcpy(token, arg, tok_len);
            token[tok_len] = '\0';

            if (*skip_ws(arg + tok_len) != '\0')
            {
                fprintf(stderr, "Syntax error line %d: HALT takes at most one operand\n", lineno);
                result = 1;
                break;
            }

            if (equals_ci(token, "OK"))
            {
                EMIT_CODE(&ob, "    HALT OK");
            }
            else if (equals_ci(token, "BAD"))
            {
                EMIT_CODE(&ob, "    HALT BAD");
            }
            else
            {
                char *endp = NULL;
                unsigned long v = strtoul(token, &endp, 0);
                if (endp == NULL || *endp != '\0')
                {
                    fprintf(stderr,
                            "Syntax error line %d: invalid HALT operand '%s'\n",
                            lineno, token);
                    result = 1;
                    break;
                }
                EMIT_CODE(&ob, "    HALT %lu", v);
            }

            if (debug)
                printf("[BASIC] HALT %s\n", token);
            continue;
        }

        if (starts_with_ci(s, "LABEL "))
        {
            const char *name = skip_ws(s + 6);
            EMIT_CODE(&ob, ".%s:", name);
            if (debug)
                printf("[BASIC] LABEL %s\n", name);
            continue;
        }

        if (starts_with_ci(s, "GOTO "))
        {
            const char *name = skip_ws(s + 5);
            EMIT_CODE(&ob, "    JMP %s", name);
            if (debug)
                printf("[BASIC] GOTO %s\n", name);
            continue;
        }

        if (starts_with_ci(s, "CALL "))
        {
            const char *name = skip_ws(s + 5);
            EMIT_CODE(&ob, "    CALL %s", name);
            if (debug)
                printf("[BASIC] CALL %s\n", name);
            continue;
        }

        if (equals_ci(s, "RETURN"))
        {
            EMIT_CODE(&ob, "    RET");
            if (debug)
                printf("[BASIC] RETURN\n");
            continue;
        }

        if (starts_with_ci(s, "INPUT "))
        {
            const char *name = skip_ws(s + 6);

            if (*name == '"')
            {
                const char *str_end = strchr(name + 1, '"');
                if (!str_end)
                {
                    fprintf(stderr, "Syntax error line %d: unterminated string in INPUT\n", lineno);
                    result = 1;
                    break;
                }

                size_t prompt_len = (size_t)(str_end - name + 1);
                char *prompt_arg = (char *)malloc(prompt_len + 1);
                if (!prompt_arg)
                {
                    fprintf(stderr, "Out of memory while parsing INPUT prompt on line %d\n", lineno);
                    result = 1;
                    break;
                }

                memcpy(prompt_arg, name, prompt_len);
                prompt_arg[prompt_len] = '\0';

                if (emit_write_values(prompt_arg, &st, &ra, &ob, debug, lineno, &uid, "WRITE"))
                {
                    free(prompt_arg);
                    result = 1;
                    break;
                }
                free(prompt_arg);

                name = skip_ws(str_end + 1);
                if (*name != ',')
                {
                    fprintf(stderr, "Syntax error line %d: expected ',' after INPUT prompt\n", lineno);
                    result = 1;
                    break;
                }

                name = skip_ws(name + 1);
            }

            Variable *v = sym_find(&st, name);
            if (!v)
            {
                fprintf(stderr, "Undefined variable '%s' on line %d\n", name, lineno);
                result = 1;
                break;
            }
            int reg = ralloc_acquire(&ra);
            char rn[4];
            reg_name(reg, rn);
            if (v->type == VAR_STR)
            {
                EMIT_CODE(&ob, "    READSTR %s", rn);
                EMIT_CODE_META(&ob, name, "    STOREVAR %s, %u", rn, v->slot);
            }
            else
            {
                EMIT_CODE(&ob, "    READ %s", rn);
                EMIT_CODE_META(&ob, name, "    STOREVAR %s, %u", rn, v->slot);
            }
            ralloc_release(&ra, reg);
            if (debug)
                printf("[BASIC] INPUT %s\n", name);
            continue;
        }
        if (starts_with_ci(s, "FOR "))
        {
            size_t slen = strlen(s);
            if (slen > 0 && s[slen - 1] == ':')
                s[slen - 1] = '\0';

            const char *p = skip_ws(s + 4);
            int inline_decl = 0;
            if (starts_with_ci(p, "VAR") &&
                !(isalnum((unsigned char)p[3]) || p[3] == '_'))
            {
                inline_decl = 1;
                p = skip_ws(p + 3);
            }

            if (!isalpha((unsigned char)*p) && *p != '_')
            {
                fprintf(stderr, "Syntax error line %d: expected FOR [VAR] <identifier> = <expr> TO <expr> [STEP <expr>]\n", lineno);
                result = 1;
                break;
            }

            char var_name[64];
            size_t vlen = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && vlen < sizeof(var_name) - 1)
                var_name[vlen++] = *p++;
            var_name[vlen] = '\0';

            p = skip_ws(p);
            if (*p != '=')
            {
                fprintf(stderr, "Syntax error line %d: expected '=' in FOR statement\n", lineno);
                result = 1;
                break;
            }

            const char *init_start = skip_ws(p + 1);
            const char *to_kw = find_keyword_token(init_start, "TO");
            if (!to_kw)
            {
                fprintf(stderr, "Syntax error line %d: expected TO in FOR statement\n", lineno);
                result = 1;
                break;
            }

            const char *to_rhs = skip_ws(to_kw + 2);
            const char *step_kw = find_keyword_token(to_rhs, "STEP");

            char *init_expr = dup_trim_range(init_start, to_kw);
            char *limit_expr = dup_trim_range(to_rhs, step_kw ? step_kw : (to_rhs + strlen(to_rhs)));
            char *step_expr = step_kw ? dup_trim_range(step_kw + 4, to_rhs + strlen(to_rhs)) : strdup("1");

            if (!init_expr || !limit_expr || !step_expr)
            {
                free(init_expr);
                free(limit_expr);
                free(step_expr);
                fprintf(stderr, "Out of memory while parsing FOR statement\n");
                result = 1;
                break;
            }

            if (*init_expr == '\0' || *limit_expr == '\0' || *step_expr == '\0')
            {
                free(init_expr);
                free(limit_expr);
                free(step_expr);
                fprintf(stderr, "Syntax error line %d: FOR requires init, TO limit, and STEP expressions\n", lineno);
                result = 1;
                break;
            }

            Variable *v = sym_find(&st, var_name);
            if (inline_decl)
            {
                if (v)
                {
                    free(init_expr);
                    free(limit_expr);
                    free(step_expr);
                    fprintf(stderr, "Error line %d: variable '%s' already defined\n", lineno, var_name);
                    result = 1;
                    break;
                }

                v = sym_add_int(&st, var_name);
                if (!v)
                {
                    free(init_expr);
                    free(limit_expr);
                    free(step_expr);
                    fprintf(stderr, "Out of memory\n");
                    result = 1;
                    break;
                }
            }
            else if (!v)
            {
                free(init_expr);
                free(limit_expr);
                free(step_expr);
                fprintf(stderr,
                        "Undefined variable '%s' on line %d (declare it first with VAR, or use FOR VAR %s = ...)\n",
                        var_name, lineno, var_name);
                result = 1;
                break;
            }
            if (v->is_const)
            {
                free(init_expr);
                free(limit_expr);
                free(step_expr);
                fprintf(stderr, "Error line %d: cannot use CONST '%s' as FOR control variable\n", lineno, var_name);
                result = 1;
                break;
            }
            if (v->type != VAR_INT)
            {
                free(init_expr);
                free(limit_expr);
                free(step_expr);
                fprintf(stderr, "Type error line %d: FOR control variable '%s' must be integer\n", lineno, var_name);
                result = 1;
                break;
            }

            uint32_t limit_slot = st.next_slot++;
            uint32_t step_slot = st.next_slot++;

            int ireg;
            if (emit_expr(init_expr, &st, &ra, &ob, debug, &ireg))
            {
                free(init_expr);
                free(limit_expr);
                free(step_expr);
                result = 1;
                break;
            }
            char irn[4];
            reg_name(ireg, irn);
            EMIT_CODE_META(&ob, var_name, "    STOREVAR %s, %u", irn, v->slot);
            ralloc_release(&ra, ireg);

            int lreg;
            if (emit_expr(limit_expr, &st, &ra, &ob, debug, &lreg))
            {
                free(init_expr);
                free(limit_expr);
                free(step_expr);
                result = 1;
                break;
            }
            char lrn[4];
            reg_name(lreg, lrn);
            EMIT_CODE_META(&ob, "for-limit", "    STOREVAR %s, %u", lrn, limit_slot);
            ralloc_release(&ra, lreg);

            int sreg;
            if (emit_expr(step_expr, &st, &ra, &ob, debug, &sreg))
            {
                free(init_expr);
                free(limit_expr);
                free(step_expr);
                result = 1;
                break;
            }
            char srn[4];
            reg_name(sreg, srn);
            EMIT_CODE_META(&ob, "for-step", "    STOREVAR %s, %u", srn, step_slot);
            ralloc_release(&ra, sreg);

            char loop_label[64], end_label[64], neg_check_label[64], body_label[64];
            snprintf(loop_label, sizeof(loop_label), "for_%lu", uid);
            snprintf(end_label, sizeof(end_label), "endfor_%lu", uid);
            snprintf(neg_check_label, sizeof(neg_check_label), "for_neg_%lu", uid);
            snprintf(body_label, sizeof(body_label), "for_body_%lu", uid);
            uid++;

            Block b;
            memset(&b, 0, sizeof(b));
            b.kind = BLOCK_FOR;
            b.for_var_slot = v->slot;
            b.for_limit_slot = limit_slot;
            b.for_step_slot = step_slot;
            strncpy(b.for_var_name, var_name, sizeof(b.for_var_name) - 1);
            snprintf(b.loop_label, sizeof(b.loop_label), ".%s", loop_label);
            snprintf(b.end_label, sizeof(b.end_label), ".%s", end_label);

            EMIT_CODE(&ob, "%s:", b.loop_label);

            int step_r = ralloc_acquire(&ra);
            int zero_r = ralloc_acquire(&ra);
            int var_r = ralloc_acquire(&ra);
            int limit_r = ralloc_acquire(&ra);
            if (step_r < 0 || zero_r < 0 || var_r < 0 || limit_r < 0)
            {
                if (step_r >= 0)
                    ralloc_release(&ra, step_r);
                if (zero_r >= 0)
                    ralloc_release(&ra, zero_r);
                if (var_r >= 0)
                    ralloc_release(&ra, var_r);
                if (limit_r >= 0)
                    ralloc_release(&ra, limit_r);
                free(init_expr);
                free(limit_expr);
                free(step_expr);
                fprintf(stderr, "Out of scratch registers\n");
                result = 1;
                break;
            }

            char step_rn[4], zero_rn[4], var_rn[4], limit_rn[4];
            reg_name(step_r, step_rn);
            reg_name(zero_r, zero_rn);
            reg_name(var_r, var_rn);
            reg_name(limit_r, limit_rn);

            EMIT_CODE_META(&ob, "for-step", "    LOADVAR %s, %u", step_rn, step_slot);
            EMIT_CODE(&ob, "    MOVI %s, 0", zero_rn);
            EMIT_CODE(&ob, "    CMP %s, %s", step_rn, zero_rn);
            EMIT_CODE(&ob, "    JL %s", neg_check_label);

            EMIT_CODE_META(&ob, var_name, "    LOADVAR %s, %u", var_rn, v->slot);
            EMIT_CODE_META(&ob, "for-limit", "    LOADVAR %s, %u", limit_rn, limit_slot);
            EMIT_CODE(&ob, "    CMP %s, %s", limit_rn, var_rn);
            EMIT_CODE(&ob, "    JL %s", b.end_label + 1);
            EMIT_CODE(&ob, "    JMP %s", body_label);

            EMIT_CODE(&ob, ".%s:", neg_check_label);
            EMIT_CODE_META(&ob, var_name, "    LOADVAR %s, %u", var_rn, v->slot);
            EMIT_CODE_META(&ob, "for-limit", "    LOADVAR %s, %u", limit_rn, limit_slot);
            EMIT_CODE(&ob, "    CMP %s, %s", var_rn, limit_rn);
            EMIT_CODE(&ob, "    JL %s", b.end_label + 1);
            EMIT_CODE(&ob, ".%s:", body_label);

            ralloc_release(&ra, step_r);
            ralloc_release(&ra, zero_r);
            ralloc_release(&ra, var_r);
            ralloc_release(&ra, limit_r);

            bstack_push(&bs, b);

            if (debug)
                printf("[BASIC] FOR %s = (%s) TO (%s) STEP (%s)\n", var_name, init_expr, limit_expr, step_expr);

            free(init_expr);
            free(limit_expr);
            free(step_expr);
            continue;
        }

        if (starts_with_ci(s, "NEXT"))
        {
            const char *arg = skip_ws(s + 4);

            Block *top = bstack_peek(&bs);
            if (!top || top->kind != BLOCK_FOR)
            {
                fprintf(stderr, "Error line %d: NEXT without FOR\n", lineno);
                result = 1;
                break;
            }

            if (*arg)
            {
                char next_var[64];
                size_t nlen = 0;
                while ((isalnum((unsigned char)arg[nlen]) || arg[nlen] == '_') && nlen < sizeof(next_var) - 1)
                    nlen++;
                if (nlen == 0)
                {
                    fprintf(stderr, "Syntax error line %d: expected NEXT [<identifier>]\n", lineno);
                    result = 1;
                    break;
                }

                memcpy(next_var, arg, nlen);
                next_var[nlen] = '\0';
                arg = skip_ws(arg + nlen);
                if (*arg != '\0')
                {
                    fprintf(stderr, "Syntax error line %d: expected NEXT [<identifier>]\n", lineno);
                    result = 1;
                    break;
                }

                if (!equals_ci(next_var, top->for_var_name))
                {
                    fprintf(stderr, "Error line %d: NEXT variable '%s' does not match FOR variable '%s'\n",
                            lineno, next_var, top->for_var_name);
                    result = 1;
                    break;
                }
            }

            Block b = bstack_pop(&bs);

            int var_r = ralloc_acquire(&ra);
            int step_r = ralloc_acquire(&ra);
            if (var_r < 0 || step_r < 0)
            {
                if (var_r >= 0)
                    ralloc_release(&ra, var_r);
                if (step_r >= 0)
                    ralloc_release(&ra, step_r);
                fprintf(stderr, "Out of scratch registers\n");
                result = 1;
                break;
            }

            char vrn[4], srn2[4];
            reg_name(var_r, vrn);
            reg_name(step_r, srn2);

            EMIT_CODE_META(&ob, b.for_var_name, "    LOADVAR %s, %u", vrn, b.for_var_slot);
            EMIT_CODE_META(&ob, "for-step", "    LOADVAR %s, %u", srn2, b.for_step_slot);
            EMIT_CODE(&ob, "    ADD %s, %s", vrn, srn2);
            EMIT_CODE_META(&ob, b.for_var_name, "    STOREVAR %s, %u", vrn, b.for_var_slot);
            EMIT_CODE(&ob, "    JMP %s", b.loop_label + 1);
            EMIT_CODE(&ob, "%s:", b.end_label);

            ralloc_release(&ra, var_r);
            ralloc_release(&ra, step_r);

            if (debug)
                printf("[BASIC] NEXT %s\n", b.for_var_name);
            continue;
        }
        if (starts_with_ci(s, "INC"))
        {
            const char *name = skip_ws(s + 4);
            Variable *v = sym_find(&st, name);
            if (!v)
            {
                fprintf(stderr, "Undefined variable '%s' on line %d\n", name, lineno);
                result = 1;
                break;
            }
            if (v->is_const)
            {
                fprintf(stderr, "Error line %d: cannot INC CONST '%s'\n", lineno, name);
                result = 1;
                break;
            }
            if (v->type != VAR_INT)
            {
                fprintf(stderr, "Type error line %d: cannot INC non-integer variable '%s'\n", lineno, name);
                result = 1;
                break;
            }
            int reg = ralloc_acquire(&ra);
            if (reg < 0)
            {
                fprintf(stderr, "Out of scratch registers on line %d\n", lineno);
                result = 1;
                break;
            }
            char rn[4];
            reg_name(reg, rn);
            EMIT_CODE_META(&ob, name, "    LOADVAR %s, %u", rn, v->slot);
            EMIT_CODE(&ob, "    INC %s", rn);
            EMIT_CODE_META(&ob, name, "    STOREVAR %s, %u", rn, v->slot);
            ralloc_release(&ra, reg);
            continue;
        }

        if (starts_with_ci(s, "DEC"))
        {
            const char *name = skip_ws(s + 4);
            Variable *v = sym_find(&st, name);
            if (!v)
            {
                fprintf(stderr, "Undefined variable '%s' on line %d\n", name, lineno);
                result = 1;
                break;
            }
            if (v->is_const)
            {
                fprintf(stderr, "Error line %d: cannot DEC CONST '%s'\n", lineno, name);
                result = 1;
                break;
            }
            if (v->type != VAR_INT)
            {
                fprintf(stderr, "Type error line %d: cannot DEC non-integer variable '%s'\n", lineno, name);
                result = 1;
                break;
            }
            int reg = ralloc_acquire(&ra);
            if (reg < 0)
            {
                fprintf(stderr, "Out of scratch registers on line %d\n", lineno);
                result = 1;
                break;
            }
            char rn[4];
            reg_name(reg, rn);
            EMIT_CODE_META(&ob, name, "    LOADVAR %s, %u", rn, v->slot);
            EMIT_CODE(&ob, "    DEC %s", rn);
            EMIT_CODE_META(&ob, name, "    STOREVAR %s, %u", rn, v->slot);
            ralloc_release(&ra, reg);
            continue;
        }

        if (equals_ci(s, "BREAK"))
        {
            Block *target = NULL;
            for (int i = bs.top - 1; i >= 0; i--)
            {
                if (bs.items[i].kind == BLOCK_WHILE || bs.items[i].kind == BLOCK_FOR)
                {
                    target = &bs.items[i];
                    break;
                }
            }

            if (!target)
            {
                fprintf(stderr, "Error line %d: BREAK outside WHILE or FOR loop\n", lineno);
                result = 1;
                break;
            }

            EMIT_CODE(&ob, "    JMP %s", target->end_label + 1);
            if (debug)
                printf("[BASIC] BREAK\n");
            continue;
        }

        fprintf(stderr, "Unknown statement on line %d: %s\n", lineno, s);
        result = 1;
        break;
    }

    fclose(in);

    if (bs.top > 0 && result == 0)
    {
        fprintf(stderr, "Error: unclosed block (%s)\n",
                bs.items[bs.top - 1].kind == BLOCK_IF
                    ? "IF"
                    : (bs.items[bs.top - 1].kind == BLOCK_WHILE ? "WHILE" : "FOR"));
        result = 1;
    }

    if (result != 0)
    {
        sym_free(&st);
        outbuf_free(&ob);
        return result;
    }

    // final .bbx
    FILE *out = fopen(output_file, "w");
    if (!out)
    {
        perror("fopen output");
        sym_free(&st);
        outbuf_free(&ob);
        return 1;
    }

    fprintf(out, "%%asm\n");

    if (ob.data_len > 0)
    {
        fprintf(out, "%%data\n");
        fwrite(ob.data_sec, 1, ob.data_len, out);
    }

    fprintf(out, "%%main\n");
    fprintf(out, "    CALL __bbx_basic_main\n");
    fprintf(out, "    HALT OK\n");

    fprintf(out, ".__bbx_basic_main:\n");
    if (st.next_slot > 0)
        fprintf(out, "    FRAME %u\n", st.next_slot);

    fwrite(ob.code_sec, 1, ob.code_len, out);
    fprintf(out, "    RET\n");

    fclose(out);
    sym_free(&st);
    outbuf_free(&ob);

    if (debug)
        printf("[BASIC] Emitted %zu data bytes, %zu code bytes, %u slots\n",
               ob.data_len, ob.code_len, st.next_slot);

    return 0;
}