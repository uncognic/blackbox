#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tools.h"
#include "define.h"
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

uint32_t find_label(const char *name, Label *labels, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (equals_ci(labels[i].name, name))
        {
            return labels[i].addr;
        }
    }
    fprintf(stderr, "Unknown label %s\n", name);
    exit(1);
}
uint32_t find_data(const char *name, Data *data, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (equals_ci(data[i].name, name))
            return i;
    }
    fprintf(stderr, "Error: undefined string constant '%s'\n", name);
    exit(1);
}
void write_u32(FILE *out, uint32_t val)
{
    fputc((val >> 0) & 0xFF, out);
    fputc((val >> 8) & 0xFF, out);
    fputc((val >> 16) & 0xFF, out);
    fputc((val >> 24) & 0xFF, out);
}
void write_u64(FILE *out, uint64_t val)
{
    fputc((val >> 0) & 0xFF, out);
    fputc((val >> 8) & 0xFF, out);
    fputc((val >> 16) & 0xFF, out);
    fputc((val >> 24) & 0xFF, out);
    fputc((val >> 32) & 0xFF, out);
    fputc((val >> 40) & 0xFF, out);
    fputc((val >> 48) & 0xFF, out);
    fputc((val >> 56) & 0xFF, out);
}
void write_i64(FILE *out, int64_t val)
{
    write_u64(out, (uint64_t)val);
}
int64_t read_i64(const uint8_t *data, size_t *pc)
{
    uint64_t b0 = (uint64_t)data[*pc + 0];
    uint64_t b1 = (uint64_t)data[*pc + 1];
    uint64_t b2 = (uint64_t)data[*pc + 2];
    uint64_t b3 = (uint64_t)data[*pc + 3];
    uint64_t b4 = (uint64_t)data[*pc + 4];
    uint64_t b5 = (uint64_t)data[*pc + 5];
    uint64_t b6 = (uint64_t)data[*pc + 6];
    uint64_t b7 = (uint64_t)data[*pc + 7];

    uint64_t u = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24) | (b4 << 32) | (b5 << 40) | (b6 << 48) | (b7 << 56);

    *pc += 8;
    return (int64_t)u;
}
uint64_t read_u64(const uint8_t *data, size_t *pc)
{
    uint64_t b0 = (uint64_t)data[*pc + 0];
    uint64_t b1 = (uint64_t)data[*pc + 1];
    uint64_t b2 = (uint64_t)data[*pc + 2];
    uint64_t b3 = (uint64_t)data[*pc + 3];
    uint64_t b4 = (uint64_t)data[*pc + 4];
    uint64_t b5 = (uint64_t)data[*pc + 5];
    uint64_t b6 = (uint64_t)data[*pc + 6];
    uint64_t b7 = (uint64_t)data[*pc + 7];

    uint64_t u = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24) | (b4 << 32) | (b5 << 40) | (b6 << 48) | (b7 << 56);

    *pc += 8;
    return u;
}
size_t instr_size(const char *line)
{
    if (starts_with_ci(line, "MOV"))
    {
        char dst[8], src[32];

        const char *p = line + 3;
        while (*p && isspace(*p))
            p++;

        const char *comma = strchr(p, ',');
        if (!comma)
            return 0;

        size_t len = comma - p;
        if (len >= sizeof(dst))
            len = sizeof(dst) - 1;
        strncpy(dst, p, len);
        dst[len] = 0;

        p = comma + 1;
        while (*p && isspace(*p))
            p++;
        strncpy(src, p, sizeof(src) - 1);
        src[sizeof(src) - 1] = 0;

        char *end = src + strlen(src) - 1;
        while (end >= src && isspace(*end))
            *end-- = 0;

        if (toupper(src[0]) == 'R')
            return 3;
        else
            return 6;
    }
    else if (starts_with_ci(line, "PUSH"))
    {
        char operand[32];
        sscanf(line + 4, " %31s", operand);
        if (operand[0] == 'R')
        {
            return 2;
        }
        else
        {
            return 5;
        }
    }
    else if (starts_with_ci(line, "POP"))
        return 2;
    else if (starts_with_ci(line, "ADD"))
        return 3;
    else if (starts_with_ci(line, "SUB"))
        return 3;
    else if (starts_with_ci(line, "MUL"))
        return 3;
    else if (starts_with_ci(line, "DIV"))
        return 3;
    else if (equals_ci(line, "PRINT_STACKSIZE"))
        return 1;
    else if (starts_with_ci(line, "PRINTREG"))
        return 2;
    else if (starts_with_ci(line, "PRINT"))
        return 2;
    else if (starts_with_ci(line, "WRITE"))
    {
        char *quote = strchr(line, '"');
        if (!quote)
            return 3;
        char *end = strchr(quote + 1, '"');
        if (!end)
            return 3;
        size_t str_len = end - (quote + 1);
        if (str_len > 255)
            str_len = 255;
        return 3 + str_len;
    }
    else if (starts_with_ci(line, "JMP"))
        return 5;
    else if (starts_with_ci(line, "ALLOC"))
        return 5;
    else if (starts_with_ci(line, "NEWLINE"))
        return 1;
    else if (starts_with_ci(line, "JE"))
        return 5;
    else if (starts_with_ci(line, "JNE"))
        return 5;
    else if (starts_with_ci(line, "INC"))
        return 2;
    else if (starts_with_ci(line, "DEC"))
        return 2;
    else if (starts_with_ci(line, "CMP"))
        return 3;
    else if (starts_with_ci(line, "STORE"))
    {
        const char *p = line + 5;
        const char *comma = strchr(p, ',');
        if (!comma)
            return 6;
        const char *q = comma + 1;
        while (*q && isspace(*q))
            q++;
        if (toupper((unsigned char)q[0]) == 'R')
            return 3;
        return 6;
    }
    else if (starts_with_ci(line, "LOADSTR"))
        return 6;
    else if (starts_with_ci(line, "LOADBYTE"))
        return 6;
    else if (starts_with_ci(line, "LOADWORD"))
        return 6;
    else if (starts_with_ci(line, "LOADDWORD"))
        return 6;
    else if (starts_with_ci(line, "LOADQWORD"))
        return 6;
    else if (starts_with_ci(line, "LOAD"))
    {
        const char *p = line + 4;
        const char *comma = strchr(p, ',');
        if (!comma)
            return 6;
        const char *q = comma + 1;
        while (*q && isspace(*q))
            q++;
        if (toupper((unsigned char)q[0]) == 'R')
            return 3;
        return 6;
    }

    else if (starts_with_ci(line, "LOADVAR"))
    {
        const char *p = line + 7;
        const char *comma = strchr(p, ',');
        if (!comma)
            return 6;
        const char *q = comma + 1;
        while (*q && isspace(*q))
            q++;
        if (toupper((unsigned char)q[0]) == 'R')
            return 3;
        return 6;
    }
    else if (starts_with_ci(line, "GROW"))
        return 5;

    else if (starts_with_ci(line, "STOREVAR"))
    {
        const char *p = line + 8;
        const char *comma = strchr(p, ',');
        if (!comma)
            return 6;
        const char *q = comma + 1;
        while (*q && isspace(*q))
            q++;
        if (toupper((unsigned char)q[0]) == 'R')
            return 3;
        return 6;
    }
    else if (starts_with_ci(line, "RESIZE"))
        return 5;
    else if (starts_with_ci(line, "FREE"))
        return 5;
    else if (starts_with_ci(line, "MOD"))
        return 3;
    else if (starts_with_ci(line, "FOPEN"))
    {
        const char *quote = strchr(line, '"');
        if (!quote)
            return 4;
        const char *end = strchr(quote + 1, '"');
        if (!end)
            return 4;
        size_t str_len = end - (quote + 1);
        if (str_len > 255)
            str_len = 255;
        return 4 + str_len;
    }
    else if (starts_with_ci(line, "FCLOSE"))
        return 2;
    else if (starts_with_ci(line, "FREAD"))
        return 3;
    else if (starts_with_ci(line, "FWRITE"))
    {
        const char *comma = strchr(line + 6, ',');
        if (!comma)
            return 6;
        const char *p = comma + 1;
        while (*p && isspace(*p))
            p++;
        if (toupper(p[0]) == 'R')
            return 3;
        else
            return 6;
    }
    else if (starts_with_ci(line, "FSEEK"))
    {
        const char *comma = strchr(line + 5, ',');
        if (!comma)
            return 6;
        const char *p = comma + 1;
        while (*p && isspace(*p))
            p++;
        if (toupper(p[0]) == 'R')
            return 3;
        else
            return 6;
    }
    else if (starts_with_ci(line, "PRINTSTR"))
        return 2;
    else if (starts_with_ci(line, "HALT")) {
        char operand[32];
        if (sscanf(line + 4, " %31s", operand) == 1)
            return 2;
        else
            return 1;
    }
    else if (starts_with_ci(line, "NOT"))
        return 2;
    else if (starts_with_ci(line, "AND"))
        return 3;
    else if (starts_with_ci(line, "OR"))
        return 3;
    else if (starts_with_ci(line, "XOR"))
        return 3;
    else if (starts_with_ci(line, "READSTR"))
        return 2;
    else if (starts_with_ci(line, "READCHAR"))
        return 2;
    else if (starts_with_ci(line, "SLEEP"))
        return 5;
    else if (starts_with_ci(line, "CLRSCR"))
        return 1;
    else if (starts_with_ci(line, "RAND"))
        return 18;
    else if (starts_with_ci(line, "GETKEY"))
        return 2;
    else if (starts_with_ci(line, "READ"))
        return 2;
    else if (equals_ci(line, "CONTINUE"))
        return 1;
    else if (starts_with_ci(line, "JL"))
        return 5;
    else if (starts_with_ci(line, "JGE"))
        return 5;
    else if (starts_with_ci(line, "JB"))
        return 5;
    else if (starts_with_ci(line, "JAE"))
        return 5;
    else if (starts_with_ci(line, "CALL"))
        return 9;
    else if (starts_with_ci(line, "RET"))
        return 1;
    else if (starts_with_ci(line, "BREAK"))
        return 1;
    fprintf(stderr, "Unknown instruction for size calculation: %s\n", line);
    exit(1);
}
uint8_t parse_register(const char *r, int lineno)
{
    if (toupper((unsigned char)r[0]) != 'R')
    {
        fprintf(stderr, "Invalid register on line %d\n", lineno);
        exit(1);
    }
    char *end;
    long v = strtol(r + 1, &end, 10);
    if (*end != '\0' || v < 0 || v >= REGISTERS)
    {
        fprintf(stderr, "Invalid register on line %d\n", lineno);
        exit(1);
    }
    return (uint8_t)v;
}
uint8_t parse_file(const char *r, int lineno)
{
    if (toupper((unsigned char)r[0]) != 'F')
    {
        fprintf(stderr, "Invalid file descriptor on line %d\n", lineno);
        exit(1);
    }
    char *end;
    long v = strtol(r + 1, &end, 10);
    if (*end != '\0' || v < 0 || v >= FILE_DESCRIPTORS)
    {
        fprintf(stderr, "Invalid file descriptor on line %d\n", lineno);
        exit(1);
    }
    return (uint8_t)v;
}
char *trim(char *s)
{
    while (isspace(*s))
        s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && (isspace(*end) || *end == '\r' || *end == '\n'))
        *end-- = '\0';
    return s;
}
uint64_t get_true_random()
{
#ifdef _WIN32
    uint64_t num;
    if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, (PUCHAR)&num, (ULONG)sizeof(num), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
    {
        fprintf(stderr, "Random generation failed\n");
        return 0;
    }
    return num;
#else
    uint64_t num = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
    {
        perror("open /dev/urandom");
        return 0;
    }

    ssize_t n = read(fd, &num, sizeof(num));
    if (n != sizeof(num))
    {
        fprintf(stderr, "Failed to read 8 bytes from /dev/urandom\n");
        close(fd);
        return 0;
    }

    close(fd);
    return num;
#endif
}

Macro *find_macro(Macro *macros, size_t macro_count, const char *name)
{
    for (size_t m = 0; m < macro_count; m++)
    {
        if (equals_ci(macros[m].name, name))
            return &macros[m];
    }
    return NULL;
}

char *replace_all(const char *src, const char *find, const char *repl)
{
    if (!find || find[0] == '\0')
        return strdup(src);

    size_t src_len = strlen(src);
    size_t find_len = strlen(find);
    size_t repl_len = strlen(repl);

    const char *p = src;
    size_t count = 0;
    while ((p = strstr(p, find)) != NULL)
    {
        count++;
        p += find_len;
    }

    size_t out_len = src_len + count * (repl_len > find_len ? repl_len - find_len : 0) + 1;
    char *out = malloc(out_len);
    if (!out)
    {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }

    char *dst = out;
    const char *cur = src;
    const char *match;
    while ((match = strstr(cur, find)) != NULL)
    {
        size_t prefix = (size_t)(match - cur);
        memcpy(dst, cur, prefix);
        dst += prefix;
        memcpy(dst, repl, repl_len);
        dst += repl_len;
        cur = match + find_len;
    }
    memcpy(dst, cur, strlen(cur) + 1);
    return out;
}

int expand_invocation(const char *invocation_line, FILE *dest, int depth, Macro *macros, size_t macro_count, unsigned long *expand_id)
{
    if (depth > 32)
        return -1;
    char *copy = strdup(invocation_line);
    char *t = trim(copy);
    if (t[0] != '%')
    {
        free(copy);
        return 0;
    }
    char *p = t + 1;
    char *name = strtok(p, " \t\r\n");
    if (!name)
    {
        free(copy);
        return 0;
    }
    Macro *m = find_macro(macros, macro_count, name);
    if (!m)
    {
        free(copy);
        return 0;
    }
    char *args[32];
    int argc = 0;
    char *at;
    while ((at = strtok(NULL, " \t\r\n")) != NULL && argc < 32)
        args[argc++] = at;

    (*expand_id)++;
    char idbuf[32];
    snprintf(idbuf, sizeof(idbuf), "M%lu", *expand_id);

    for (int bi = 0; bi < m->bodyc; bi++)
    {
        char *line = strdup(m->body[bi]);
        for (int pi = 0; pi < m->paramc; pi++)
        {
            char findbuf[128];
            snprintf(findbuf, sizeof(findbuf), "$%s", m->params[pi]);
            const char *repl = (pi < argc) ? args[pi] : "";
            char *tmp = replace_all(line, findbuf, repl);
            free(line);
            line = tmp;
        }
        for (int pi = 0; pi < argc; pi++)
        {
            char findbuf[8];
            snprintf(findbuf, sizeof(findbuf), "$%d", pi + 1);
            char *tmp = replace_all(line, findbuf, args[pi]);
            free(line);
            line = tmp;
        }

        char *pcur = line;
        size_t outcap = strlen(line) + 1;
        char *out = malloc(outcap);
        if (!out)
        {
            fprintf(stderr, "Out of memory\n");
            exit(1);
        }
        out[0] = '\0';
        for (;;)
        {
            char *atpos = strstr(pcur, "@@");
            if (!atpos)
            {
                size_t need = strlen(out) + strlen(pcur) + 1;
                if (need > outcap)
                {
                    outcap = need;
                    out = realloc(out, outcap);
                    if (!out)
                    {
                        fprintf(stderr, "Out of memory\n");
                        exit(1);
                    }
                }
                strcat(out, pcur);
                break;
            }
            size_t prefixlen = (size_t)(atpos - pcur);
            size_t need = strlen(out) + prefixlen + 1;
            if (need > outcap)
            {
                outcap = need * 2;
                out = realloc(out, outcap);
                if (!out)
                {
                    fprintf(stderr, "Out of memory\n");
                    exit(1);
                }
            }
            strncat(out, pcur, prefixlen);
            char *ident = atpos + 2;
            int il = 0;
            while (ident[il] && (isalnum((unsigned char)ident[il]) || ident[il] == '_'))
                il++;
            char identbuf[128];
            strncpy(identbuf, ident, il);
            identbuf[il] = '\0';
            char replbuf[256];
            snprintf(replbuf, sizeof(replbuf), "%s_%s", idbuf, identbuf);
            size_t need2 = strlen(out) + strlen(replbuf) + 1;
            if (need2 > outcap)
            {
                outcap = need2;
                out = realloc(out, outcap);
                if (!out)
                {
                    fprintf(stderr, "Out of memory\n");
                    exit(1);
                }
            }
            strcat(out, replbuf);
            pcur = ident + il;
        }

        char *wline = trim(out);
        if (wline[0] == '%')
        {
            expand_invocation(wline, dest, depth + 1, macros, macro_count, expand_id);
        }
        else
        {
            fputs(wline, dest);
            size_t l = strlen(wline);
            if (l == 0 || wline[l - 1] != '\n')
                fputc('\n', dest);
        }

        free(out);
        free(line);
    }

    free(copy);
    return 1;
}
int equals_ci(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

int starts_with_ci(const char *s, const char *prefix)
{
    if (!s || !prefix)
        return 0;
    size_t i = 0;
    while (prefix[i]) {
        if (s[i] == '\0')
            return 0;
        if (toupper((unsigned char)s[i]) != toupper((unsigned char)prefix[i]))
            return 0;
        i++;
    }
    return 1;
}