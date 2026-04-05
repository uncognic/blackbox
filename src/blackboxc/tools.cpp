#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include "tools.h"
#include "../define.h"
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

static inline unsigned char ascii_upper(unsigned char c)
{
    return (unsigned char)toupper(c);
}

static bool equals_ci_str(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        if (ascii_upper((unsigned char)a[i]) != ascii_upper((unsigned char)b[i]))
            return false;
    }
    return true;
}

static bool starts_with_ci_str(const std::string &s, const std::string &prefix)
{
    if (s.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); i++)
    {
        if (ascii_upper((unsigned char)s[i]) != ascii_upper((unsigned char)prefix[i]))
            return false;
    }
    return true;
}

static std::string trim_copy(const std::string &s)
{
    size_t start = 0;
    while (start < s.size() && isspace((unsigned char)s[start]))
        start++;

    size_t end = s.size();
    while (end > start && (isspace((unsigned char)s[end - 1]) || s[end - 1] == '\r' || s[end - 1] == '\n'))
        end--;

    return s.substr(start, end - start);
}

static bool second_operand_is_reg(const char *line, size_t op_len)
{
    const char *p = line + op_len;
    const char *comma = strchr(p, ',');
    if (!comma)
        return false;
    const char *q = comma + 1;
    while (*q && isspace((unsigned char)*q))
        q++;
    return ascii_upper((unsigned char)q[0]) == 'R';
}

static size_t quoted_payload_size_or(const std::string &line, size_t fallback)
{
    size_t first = line.find('"');
    if (first == std::string::npos)
        return fallback;
    size_t second = line.find('"', first + 1);
    if (second == std::string::npos)
        return fallback;
    size_t len = second - (first + 1);
    return len > 255 ? 255 : len;
}

static std::string operand_after_opcode(const std::string &line, size_t op_len)
{
    if (line.size() <= op_len)
        return std::string();
    return trim_copy(line.substr(op_len));
}

static bool preprocess_includes_impl(const char *input, int depth, std::string &out)
{
    if (depth > 32)
    {
        fprintf(stderr, "Error: include nesting too deep\n");
        return false;
    }

    const std::string input_path(input);
    const size_t sep = input_path.find_last_of("/\\");
    const std::string base_dir = (sep == std::string::npos) ? std::string() : input_path.substr(0, sep);

    FILE *in = fopen(input, "rb");
    if (!in)
    {
        perror("fopen");
        return false;
    }

    out.clear();
    out.reserve(4096);

    char line[8192];
    while (fgets(line, sizeof line, in))
    {
        std::string source(line);
        std::string trimmed = trim_copy(source);
        size_t comment_pos = trimmed.find(';');
        if (comment_pos != std::string::npos)
            trimmed.erase(comment_pos);
        trimmed = trim_copy(trimmed);
        const char *s = trimmed.c_str();

        if (starts_with_ci(s, "%include"))
        {
            const char *p = s + 8;
            while (*p && isspace((unsigned char)*p))
                p++;

            if (*p != '"')
            {
                fprintf(stderr, "Error: malformed %%include directive\n");
                fclose(in);
                return false;
            }

            const char *end = strchr(p + 1, '"');
            if (!end)
            {
                fprintf(stderr, "Error: malformed %%include directive\n");
                fclose(in);
                return false;
            }

            std::string include_target(p + 1, (size_t)(end - (p + 1)));

            std::string include_path;
            if (!base_dir.empty())
                include_path = base_dir + "/" + include_target;
            else
                include_path = include_target;

            std::string included;
            if (!preprocess_includes_impl(include_path.c_str(), depth + 1, included))
            {
                fclose(in);
                return false;
            }
            out += included;
            continue;
        }

        out += line;
    }

    fclose(in);
    return true;
}

bool preprocess_includes(const std::string &input, std::string &out)
{
    return preprocess_includes_impl(input.c_str(), 0, out);
}

uint32_t find_label(const char *name, Label *labels, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp(labels[i].name, name) == 0)
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

size_t instr_size(const char *line)
{
    std::string line_sv(line);

    if (starts_with_ci(line, "MOV"))
    {
        std::string rest = line_sv.size() > 3 ? line_sv.substr(3) : std::string();
        size_t comma = rest.find(',');
        if (comma == std::string::npos)
            return 0;

        std::string src = trim_copy(rest.substr(comma + 1));
        if (!src.empty() && ascii_upper((unsigned char)src[0]) == 'R')
            return 3;
        else
            return 6;
    }
    else if (starts_with_ci(line, "PUSH"))
        return 2;
    else if (starts_with_ci(line, "PUSHI"))
        return 5;
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
    else if (starts_with_ci(line, "EPRINTREG"))
        return 2;
    else if (starts_with_ci(line, "PRINT"))
        return 2;
    else if (starts_with_ci(line, "WRITE"))
    {
        return 3 + quoted_payload_size_or(line_sv, 0);
    }
    else if (starts_with_ci(line, "EXEC"))
    {
        return 3 + quoted_payload_size_or(line_sv, 0);
    }
    else if (starts_with_ci(line, "JMPI"))
        return 5;
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
        if (!strchr(line + 5, ','))
            return 6;
        if (second_operand_is_reg(line, 5))
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
        if (!strchr(line + 4, ','))
            return 6;
        if (second_operand_is_reg(line, 4))
            return 3;
        return 6;
    }

    else if (starts_with_ci(line, "LOADVAR"))
    {
        if (!strchr(line + 7, ','))
            return 6;
        if (second_operand_is_reg(line, 7))
            return 3;
        return 6;
    }
    else if (starts_with_ci(line, "GROW"))
        return 5;

    else if (starts_with_ci(line, "STOREVAR"))
    {
        if (!strchr(line + 8, ','))
            return 6;
        if (second_operand_is_reg(line, 8))
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
        return 4 + quoted_payload_size_or(line_sv, 0);
    }
    else if (starts_with_ci(line, "FCLOSE"))
        return 2;
    else if (starts_with_ci(line, "FREAD"))
        return 3;
    else if (starts_with_ci(line, "FWRITE"))
    {
        if (!strchr(line + 6, ','))
            return 6;
        if (second_operand_is_reg(line, 6))
            return 3;
        else
            return 6;
    }
    else if (starts_with_ci(line, "FSEEK"))
    {
        if (!strchr(line + 5, ','))
            return 6;
        if (second_operand_is_reg(line, 5))
            return 3;
        else
            return 6;
    }
    else if (starts_with_ci(line, "PRINTSTR"))
        return 2;
    else if (starts_with_ci(line, "EPRINTSTR"))
        return 2;
    else if (starts_with_ci(line, "HALT"))
    {
        std::string operand = operand_after_opcode(line_sv, 4);
        if (!operand.empty())
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
    {
        std::string operand = operand_after_opcode(line_sv, 5);
        if (!operand.empty() && ascii_upper((unsigned char)operand[0]) == 'R')
            return 2; // opcode + register
        return 5; // opcode + u32 immediate
    }
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
    else if (starts_with_ci(line, "REGSYSCALL"))
        return 6;
    else if (starts_with_ci(line, "SYSCALL"))
        return 2;
    else if (starts_with_ci(line, "SYSRET"))
        return 1;
    else if (starts_with_ci(line, "DROPPRIV"))
        return 1;
    else if (starts_with_ci(line, "GETMODE"))
        return 2;
    else if (starts_with_ci(line, "SETPERM"))
        return 13;
    else if (starts_with_ci(line, "REGFAULT"))
        return 6;
    else if (starts_with_ci(line, "FAULTRET"))
        return 1;
    else if (starts_with_ci(line, "GETFAULT"))
        return 2;
    else if (starts_with_ci(line, "DUMPREGS"))
        return 1;
    else if (starts_with_ci(line, "PRINTCHAR"))
        return 2;
    else if (starts_with_ci(line, "EPRINTCHAR"))
        return 2;
    else if (starts_with_ci(line, "SHL"))
        return 3; // opcode + reg + reg 
    else if (starts_with_ci(line, "SHR"))
        return 3; // opcode + reg + reg
    else if (starts_with_ci(line, "SHLI"))
        return 10; // opcode + reg + u64 immediate
    else if (starts_with_ci(line, "SHRI"))
        return 10; // opcode + reg + u64 immediate
    else if (starts_with_ci(line, "GETARG"))
        return 6; // opcode + reg + u32 index
    else if (starts_with_ci(line, "GETARGC"))
        return 2; // opcode + reg
    else if (starts_with_ci(line, "GETENV"))
    {
        size_t comma = line_sv.find(',', 6);
        if (comma == std::string::npos)
            return 3;

        std::string operand = trim_copy(line_sv.substr(comma + 1));
        if (operand.empty())
            return 3;

        size_t len = 0;
        if (operand[0] == '"')
        {
            size_t end = operand.find('"', 1);
            if (end == std::string::npos)
                return 3;
            len = end - 1;
        }
        else
        {
            while (len < operand.size() && operand[len] != '\r' && operand[len] != '\n' && !isspace((unsigned char)operand[len]))
                len++;
        }
        if (len > 255)
            len = 255;
        return 3 + len;
    }
    fprintf(stderr, "Unknown instruction for size calculation: %s\n", line);
    exit(1);
}
uint8_t parse_register(const char *r, int lineno)
{
    if (ascii_upper((unsigned char)r[0]) != 'R')
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
    if (ascii_upper((unsigned char)r[0]) != 'F')
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
    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return s;

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
    const std::string needle(name ? name : "");
    for (size_t m = 0; m < macro_count; m++)
    {
        if (std::string(macros[m].name) == needle)
            return &macros[m];
    }
    return NULL;
}

std::string replace_all(const std::string &src, const std::string &find, const std::string &repl)
{
    if (find.empty())
        return src;

    std::string out(src);
    size_t pos = 0;
    while ((pos = out.find(find, pos)) != std::string::npos)
    {
        out.replace(pos, find.size(), repl);
        pos += repl.size();
    }

    return out;
}

static std::vector<std::string> split_tokens(const std::string &s)
{
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == ','))
            i++;
        if (i >= s.size())
            break;
        size_t start = i;
        while (i < s.size() && !isspace((unsigned char)s[i]) && s[i] != ',')
            i++;
        tokens.emplace_back(s.substr(start, i - start));
    }
    return tokens;
}

static std::string replace_all_cpp(std::string text, const std::string &needle, const std::string &replacement)
{
    if (needle.empty())
        return text;

    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos)
    {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

int expand_invocation(const char *invocation_line, FILE *dest, int depth, Macro *macros, size_t macro_count, unsigned long *expand_id)
{
    if (depth > 32)
        return -1;

    std::vector<char> inv_mut(invocation_line, invocation_line + strlen(invocation_line));
    inv_mut.push_back('\0');
    char *t = trim(inv_mut.data());
    if (t[0] != '%')
        return 0;

    std::vector<std::string> tokens = split_tokens(std::string(t + 1));
    if (tokens.empty())
        return 0;

    Macro *m = find_macro(macros, macro_count, tokens[0].c_str());
    if (!m)
        return 0;

    std::vector<std::string> args;
    for (size_t i = 1; i < tokens.size() && args.size() < 32; i++)
        args.push_back(tokens[i]);

    (*expand_id)++;
    std::string id_prefix = "M" + std::to_string(*expand_id);

    for (int bi = 0; bi < m->bodyc; bi++)
    {
        std::string line = m->body[bi];
        for (int pi = 0; pi < m->paramc; pi++)
        {
            std::string find = "$" + std::string(m->params[pi]);
            const std::string repl = (pi < (int)args.size()) ? args[(size_t)pi] : std::string();
            line = replace_all_cpp(std::move(line), find, repl);
        }
        for (size_t pi = 0; pi < args.size(); pi++)
        {
            std::string find = "$" + std::to_string(pi + 1);
            line = replace_all_cpp(std::move(line), find, args[pi]);
        }

        const char *pcur = line.c_str();
        std::string out;
        out.reserve(line.size() + 16);
        for (;;)
        {
            const char *atpos = strstr(pcur, "@@");
            if (!atpos)
            {
                out += pcur;
                break;
            }
            size_t prefixlen = (size_t)(atpos - pcur);
            out.append(pcur, prefixlen);
            const char *ident = atpos + 2;
            int il = 0;
            while (ident[il] && (isalnum((unsigned char)ident[il]) || ident[il] == '_'))
                il++;
            out += id_prefix;
            out += "_";
            out.append(ident, (size_t)il);
            pcur = ident + il;
        }

        std::vector<char> out_mut(out.begin(), out.end());
        out_mut.push_back('\0');
        char *wline = trim(out_mut.data());
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
    }

    return 1;
}
int equals_ci(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    return equals_ci_str(std::string(a), std::string(b)) ? 1 : 0;
}

int starts_with_ci(const char *s, const char *prefix)
{
    if (!s || !prefix)
        return 0;
    return starts_with_ci_str(std::string(s), std::string(prefix)) ? 1 : 0;
}