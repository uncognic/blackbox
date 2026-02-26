#include "asm.h"
#include "../define.h"
#include "../tools.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int assemble_file(const char *filename, const char *output_file, int debug)
{
    FILE *in = fopen(filename, "rb");
    if (!in)
    {
        perror("fopen input");
        return 1;
    }
    {
        char readline[8192];
        char **lines = NULL;
        size_t lines_count = 0, lines_cap = 0;

        while (fgets(readline, sizeof(readline), in))
        {
            if (lines_count + 1 >= lines_cap)
            {
                lines_cap = lines_cap ? lines_cap * 2 : 256;
                lines = realloc(lines, lines_cap * sizeof(char *));
            }
            lines[lines_count++] = strdup(readline);
        }

        fclose(in);

        FILE *tmp = tmpfile();
        if (!tmp)
        {
            perror("tmpfile");
            for (size_t i = 0; i < lines_count; i++)
                free(lines[i]);
            free(lines);
            return 1;
        }

        Macro *macros = NULL;
        size_t macro_count = 0, macro_cap = 0;

        for (size_t i = 0; i < lines_count; i++)
        {
            char *copy = strdup(lines[i]);
            char *t = trim(copy);
            if (starts_with_ci(t, "%macro") &&
                (t[6] == ' ' || t[6] == '\t' || t[6] == '\0'))
            {
                char *p = t + 6;
                while (*p == ' ' || *p == '\t')
                    p++;
                char *tok = strtok(p, " \t\r\n");
                if (!tok)
                {
                    fprintf(stderr, "Syntax error: bad %%macro header\n");
                    free(copy);
                    continue;
                }
                char *mname = strdup(tok);
                char **params = NULL;
                int paramc = 0, paramcap = 0;
                char *argtok;
                while ((argtok = strtok(NULL, " \t\r\n")) != NULL)
                {
                    if (paramc + 1 >= paramcap)
                    {
                        paramcap = paramcap ? paramcap * 2 : 8;
                        params = realloc(params, paramcap * sizeof(char *));
                    }
                    params[paramc++] = strdup(argtok);
                }

                char **body = NULL;
                int bodyc = 0, bodycap = 0;
                size_t j = i + 1;
                for (; j < lines_count; j++)
                {
                    char *c2 = strdup(lines[j]);
                    char *t2 = trim(c2);
                    if (equals_ci(t2, "%endmacro"))
                    {
                        free(c2);
                        break;
                    }
                    if (bodyc + 1 >= bodycap)
                    {
                        bodycap = bodycap ? bodycap * 2 : 16;
                        body = realloc(body, bodycap * sizeof(char *));
                    }
                    body[bodyc++] = strdup(lines[j]);
                    free(c2);
                }

                if (macro_count + 1 >= macro_cap)
                {
                    macro_cap = macro_cap ? macro_cap * 2 : 16;
                    macros = realloc(macros, macro_cap * sizeof(Macro));
                }
                macros[macro_count].name = mname;
                macros[macro_count].params = params;
                macros[macro_count].paramc = paramc;
                macros[macro_count].body = body;
                macros[macro_count].bodyc = bodyc;
                macro_count++;

                i = j;
            }
            free(copy);
        }

        unsigned long expand_id = 0;

        for (size_t i = 0; i < lines_count; i++)
        {
            char *copy = strdup(lines[i]);
            char *t = trim(copy);
            if (strncmp(t, "%macro", 6) == 0)
            {
                size_t j = i + 1;
                for (; j < lines_count; j++)
                {
                    char *c2 = strdup(lines[j]);
                    char *t2 = trim(c2);
                    if (equals_ci(t2, "%endmacro"))
                    {
                        free(c2);
                        break;
                    }
                    free(c2);
                }
                i = j;
                free(copy);
                continue;
            }
            if (t[0] == '%')
            {
                if (equals_ci(t, "%asm") || equals_ci(t, "%data") ||
                    equals_ci(t, "%main") || equals_ci(t, "%entry") ||
                    equals_ci(t, "%endmacro"))
                {
                    fputs(lines[i], tmp);
                    free(copy);
                    continue;
                }
                if (expand_invocation(t, tmp, 0, macros, macro_count,
                                      &expand_id))
                {
                    free(copy);
                    continue;
                }
            }
            fputs(lines[i], tmp);
            free(copy);
        }

        for (size_t i = 0; i < lines_count; i++)
            free(lines[i]);
        free(lines);
        for (size_t m = 0; m < macro_count; m++)
        {
            free(macros[m].name);
            for (int p = 0; p < macros[m].paramc; p++)
                free(macros[m].params[p]);
            free(macros[m].params);
            for (int b = 0; b < macros[m].bodyc; b++)
                free(macros[m].body[b]);
            free(macros[m].body);
        }
        free(macros);

        rewind(tmp);
        in = tmp;
    }
    FILE *out = fopen(output_file, "wb");
    if (!out)
    {
        perror("fopen output");
        fclose(in);
        return 1;
    }

    char line[8192];
    int lineno = 0;

    while (fgets(line, sizeof(line), in))
    {
        lineno++;
        char *s = trim(line);

        char *comment = strchr(s, ';');
        if (comment)
        {
            *comment = '\0';

            s = trim(s);
        }
        if (*s == '\0')
            continue;

        if (equals_ci(s, "%asm"))
        {
            break;
        }
        else
        {
            fprintf(stderr, "Error: file must start with %%asm (line %d)\n",
                    lineno);
            fclose(in);
            fclose(out);
            return 1;
        }
    }

    Label labels[256];
    size_t label_count = 0;
    uint32_t pc = MAGIC_SIZE;

    Data data[256];
    size_t data_count = 0;
    uint32_t data_table_size = 0;
    char data_table[8192];

    int current_section = 0;
    int found_code_section = 0;

    while (fgets(line, sizeof(line), in))
    {
        lineno++;
        char *s = trim(line);
        char *comment = strchr(s, ';');
        if (comment)
        {
            *comment = '\0';
            s = trim(s);
        }
        if (*s == '\0' || *s == ';')
            continue;

        if (equals_ci(s, "%data"))
        {
            if (found_code_section)
            {
                fprintf(stderr,
                        "Error on line %d: %%data section must come before "
                        "%%main/%%entry\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            current_section = 1;
            if (debug)
                printf("[DEBUG] Entering data section at line %d\n", lineno);
            continue;
        }
        if (equals_ci(s, "%main") == 1 || equals_ci(s, "%entry") == 1)
        {
            current_section = 2;
            found_code_section = 1;
            if (debug)
                printf("[DEBUG] Entering code section at line %d\n", lineno);
            continue;
        }

        if (starts_with_ci(s, "STR "))
        {
            if (current_section != 1)
            {
                fprintf(stderr,
                        "Error on line %d: STR must be inside %%data section\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char name[32];
            char *quote_start = strchr(s, '"');
            if (!quote_start || sscanf(s + 4, " $%31[^,]", name) != 1)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected STR $name, \"value\"\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }

            quote_start++;
            char *quote_end = strchr(quote_start, '"');
            if (!quote_end)
            {
                fprintf(stderr, "Syntax error line %d: missing closing quote\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            size_t len = quote_end - quote_start;

            snprintf(data[data_count].name, sizeof(data[data_count].name), "%s",
                     name);
            data[data_count].type = DATA_STRING;
            data[data_count].offset = data_table_size;
            data[data_count].str = &data_table[data_table_size];

            memcpy(data_table + data_table_size, quote_start, len);
            data_table[data_table_size + len] = '\0';
            data_table_size += len + 1;
            data_count++;

            if (debug)
                printf("[DEBUG] String $%s at offset %u\n", name,
                       data[data_count - 1].offset);
            continue;
        }
        else if (starts_with_ci(s, "DWORD "))
        {
            if (current_section != 1)
            {
                fprintf(
                    stderr,
                    "Error on line %d: DWORD must be inside %%data section\n",
                    lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char name[32];
            uint32_t value;
            if (sscanf(s + 6, " $%31[^,], %u", name, &value) != 2)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected DWORD $name, value\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }

            snprintf(data[data_count].name, sizeof(data[data_count].name), "%s",
                     name);
            data[data_count].type = DATA_DWORD;
            data[data_count].offset = data_table_size;
            data[data_count].dword = value;

            data_table[data_table_size + 0] = (value >> 0) & 0xFF;
            data_table[data_table_size + 1] = (value >> 8) & 0xFF;
            data_table[data_table_size + 2] = (value >> 16) & 0xFF;
            data_table[data_table_size + 3] = (value >> 24) & 0xFF;
            data_table_size += sizeof(value);
            data_count++;

            if (debug)
                printf("[DEBUG] DWORD $%s at offset %u\n", name,
                       data[data_count - 1].offset);
            continue;
        }
        else if (starts_with_ci(s, "QWORD "))
        {
            if (current_section != 1)
            {
                fprintf(
                    stderr,
                    "Error on line %d: QWORD must be inside %%data section\n",
                    lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char name[32];
            char value_str[64];
            uint64_t value;
            if (sscanf(s + 6, " $%31[^,], %63s", name, value_str) != 2)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected QWORD $name, value\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            value = (uint64_t)strtoull(value_str, NULL, 0);

            snprintf(data[data_count].name, sizeof(data[data_count].name), "%s",
                     name);
            data[data_count].type = DATA_QWORD;
            data[data_count].offset = data_table_size;
            data[data_count].qword = value;

            data_table[data_table_size + 0] = (value >> 0) & 0xFF;
            data_table[data_table_size + 1] = (value >> 8) & 0xFF;
            data_table[data_table_size + 2] = (value >> 16) & 0xFF;
            data_table[data_table_size + 3] = (value >> 24) & 0xFF;
            data_table[data_table_size + 4] = (value >> 32) & 0xFF;
            data_table[data_table_size + 5] = (value >> 40) & 0xFF;
            data_table[data_table_size + 6] = (value >> 48) & 0xFF;
            data_table[data_table_size + 7] = (value >> 56) & 0xFF;

            data_table_size += sizeof(value);
            data_count++;

            if (debug)
                printf("[DEBUG] QWORD $%s at offset %u\n", name,
                       data[data_count - 1].offset);
            continue;
        }
        else if (starts_with_ci(s, "WORD "))
        {
            if (current_section != 1)
            {
                fprintf(
                    stderr,
                    "Error on line %d: WORD must be inside %%data section\n",
                    lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char name[32];
            uint16_t value;
            if (sscanf(s + 5, " $%31[^,], %hu", name, &value) != 2)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected WORD $name, value\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }

            snprintf(data[data_count].name, sizeof(data[data_count].name), "%s",
                     name);
            data[data_count].type = DATA_WORD;
            data[data_count].offset = data_table_size;
            data[data_count].word = value;

            data_table[data_table_size + 0] = (value >> 0) & 0xFF;
            data_table[data_table_size + 1] = (value >> 8) & 0xFF;

            data_table_size += sizeof(value);
            data_count++;

            if (debug)
                printf("[DEBUG] WORD $%s at offset %u\n", name,
                       data[data_count - 1].offset);
            continue;
        }
        else if (starts_with_ci(s, "BYTE "))
        {
            if (current_section != 1)
            {
                fprintf(
                    stderr,
                    "Error on line %d: BYTE must be inside %%data section\n",
                    lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char name[32];
            uint8_t value;
            if (sscanf(s + 5, " $%31[^,], %hhu", name, &value) != 2)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected BYTE $name, value\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }

            snprintf(data[data_count].name, sizeof(data[data_count].name), "%s",
                     name);
            data[data_count].type = DATA_BYTE;
            data[data_count].offset = data_table_size;
            data[data_count].byte = value;

            memcpy(data_table + data_table_size, &value, sizeof(value));
            data_table_size += sizeof(value);
            data_count++;

            if (debug)
                printf("[DEBUG] BYTE $%s at offset %u\n", name,
                       data[data_count - 1].offset);
            continue;
        }

        if (current_section != 2)
        {
            size_t len = strlen(s);
            if (s[0] == '.' && s[len - 1] == ':')
            {
                fprintf(stderr,
                        "Error on line %d: labels must be inside "
                        "%%main/%%entry section\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (current_section == 0)
            {
                fprintf(stderr,
                        "Error on line %d: code outside of section. Use "
                        "%%string or %%main/%%entry\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            continue;
        }

        size_t len = strlen(s);
        if (s[0] == '.' && s[len - 1] == ':')
        {
            if (label_count >= 256)
            {
                fprintf(stderr, "Too many labels (max 256)\n");
                fclose(in);
                fclose(out);
                return 1;
            }

            s[len - 1] = '\0';
            strncpy(labels[label_count].name, s + 1,
                    sizeof(labels[label_count].name) - 1);
            labels[label_count].name[sizeof(labels[label_count].name) - 1] =
                '\0';
            labels[label_count].addr = pc;
            labels[label_count].frame_size = 0;
            if (debug)
            {
                printf("[DEBUG] Label %s at pc=%u\n", labels[label_count].name,
                       (unsigned)labels[label_count].addr);
            }
            label_count++;
            continue;
        }

        if (starts_with_ci(s, "FRAME"))
        {
            if (label_count == 0)
            {
                fprintf(stderr, "Error on line %d: FRAME must follow a label\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char framebuf[32];
            if (sscanf(s + 5, " %31s", framebuf) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected FRAME <slots>\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t fs = (uint32_t)strtoul(framebuf, NULL, 0);
            labels[label_count - 1].frame_size = fs;
            continue;
        }

        pc += (uint32_t)instr_size(s);
    }

    if (!found_code_section)
    {
        fprintf(stderr, "Error: missing %%main or %%entry section\n");
        fclose(in);
        fclose(out);
        return 1;
    }

    uint32_t header_offset = (HEADER_FIXED_SIZE - MAGIC_SIZE) + data_table_size;
    for (size_t i = 0; i < label_count; i++)
    {
        labels[i].addr += header_offset;
    }

    rewind(in);
    lineno = 0;
    current_section = 0;

    fputc((MAGIC >> 16) & 0xFF, out);
    fputc((MAGIC >> 8) & 0xFF, out);
    fputc((MAGIC >> 0) & 0xFF, out);
    fputc(data_count, out);
    write_u32(out, data_table_size);
    fwrite(data_table, 1, data_table_size, out);

    while (fgets(line, sizeof(line), in))
    {
        lineno++;
        char *s = trim(line);
        char *comment = strchr(s, ';');
        if (comment)
        {
            *comment = '\0';
            s = trim(s);
        }
        if (*s == '\0' || *s == ';')
            continue;
        if (equals_ci(s, "%data"))
        {
            current_section = 1;
            continue;
        }
        if (equals_ci(s, "%main") == 1 || equals_ci(s, "%entry") == 1)
        {
            current_section = 2;
            continue;
        }

        if (current_section == 1)
            continue;

        if (current_section != 2)
            continue;

        if (starts_with_ci(s, "STR "))
        {
            fprintf(stderr,
                    "Syntax error on line %d: STR directive not allowed in "
                    "code section\n",
                    lineno);
            fclose(in);
            fclose(out);
            return 1;
        }
        if (starts_with_ci(s, "DWORD "))
        {
            fprintf(stderr,
                    "Syntax error on line %d: DWORD directive not allowed in "
                    "code section\n",
                    lineno);
            fclose(in);
            fclose(out);
            return 1;
        }

        if (starts_with_ci(s, "QWORD "))
        {
            fprintf(stderr,
                    "Syntax error on line %d: QWORD directive not allowed in "
                    "code section\n",
                    lineno);
            fclose(in);
            fclose(out);
            return 1;
        }
        if (starts_with_ci(s, "WORD "))
        {
            fprintf(stderr,
                    "Syntax error on line %d: WORD directive not allowed in "
                    "code section\n",
                    lineno);
            fclose(in);
            fclose(out);
            return 1;
        }
        if (starts_with_ci(s, "BYTE "))
        {
            fprintf(stderr,
                    "Syntax error on line %d: BYTE directive not allowed in "
                    "code section\n",
                    lineno);
            fclose(in);
            fclose(out);
            return 1;
        }
        else if (starts_with_ci(s, "WRITE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char fd_str[16];
            char *str_start;

            if (sscanf(s + 5, " %15s", fd_str) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected WRITE <stdout|stderr> "
                        "\"<string>\"\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t fd;
            if (equals_ci(fd_str, "stdout"))
            {
                fd = 1;
            }
            else if (equals_ci(fd_str, "stderr"))
            {
                fd = 2;
            }
            else
            {
                fprintf(stderr,
                        "Invalid file descriptor on line %d: %s (expected "
                        "stdout or stderr)\n",
                        lineno, fd_str);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start = strchr(s, '"');
            if (!str_start)
            {
                fprintf(stderr,
                        "Syntax error on line %d: missing opening quote for "
                        "string\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start++;

            char *str_end = strchr(str_start, '"');
            if (!str_end)
            {
                fprintf(stderr,
                        "Syntax error on line %d: missing closing quote for "
                        "string\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            size_t len = str_end - str_start;
            if (len > 255)
                len = 255;

            fputc(OPCODE_WRITE, out);
            fputc(fd, out);
            fputc((uint8_t)len, out);

            for (size_t i = 0; i < len; i++)
            {
                fputc((uint8_t)str_start[i], out);
            }
        }
        else if (starts_with_ci(s, "LOADSTR"))
        {
            char name[32];
            char regname[16];
            if (sscanf(s + 7, " $%31[^,], %15s", name, regname) != 2)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected LOADSTR $name, "
                        "<register>\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = find_data(name, data, data_count);
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADSTR, out);
            fputc(reg, out);
            uint32_t mem_offset = data[offset].offset;
            write_u32(out, mem_offset);
            if (debug)
                printf("[DEBUG] LOADSTR $%s (offset=%u) -> %s\n", name, offset,
                       regname);
        }
        else if (starts_with_ci(s, "PRINTSTR"))
        {
            if (debug)
                printf("[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            if (sscanf(s + 8, " %15s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected PRINTSTR <register>\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_PRINTSTR, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "LOADBYTE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char name[32];
            char regname[16];
            if (sscanf(s + 8, " $%31[^,], %15s", name, regname) != 2)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected LOADBYTE $name, "
                        "<register>\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = find_data(name, data, data_count);
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADBYTE, out);
            fputc(reg, out);
            Data *d = &data[offset];
            uint32_t offset_in_table = d->offset;
            write_u32(out, offset_in_table);
            if (debug)
                printf("[DEBUG] LOADBYTE $%s (offset=%u) -> %s\n", name, offset,
                       regname);
        }
        else if (starts_with_ci(s, "LOADWORD"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char name[32];
            char regname[16];
            if (sscanf(s + 8, " $%31[^,], %15s", name, regname) != 2)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected LOADWORD $name, "
                        "<register>\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = find_data(name, data, data_count);
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADWORD, out);
            fputc(reg, out);
            Data *d = &data[offset];
            uint32_t offset_in_table = d->offset;
            write_u32(out, offset_in_table);
            if (debug)
                printf("[DEBUG] LOADWORD $%s (offset=%u) -> %s\n", name, offset,
                       regname);
        }
        else if (starts_with_ci(s, "LOADDWORD"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char name[32];
            char regname[16];
            if (sscanf(s + 9, " $%31[^,], %15s", name, regname) != 2)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected LOADDWORD $name, "
                        "<register>\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = find_data(name, data, data_count);
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADDWORD, out);
            fputc(reg, out);
            Data *d = &data[offset];
            uint32_t offset_in_table = d->offset;
            write_u32(out, offset_in_table);
            if (debug)
                printf("[DEBUG] LOADDWORD $%s (offset=%u) -> %s\n", name,
                       offset, regname);
        }
        else if (starts_with_ci(s, "LOADQWORD"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char name[32];
            char regname[16];
            if (sscanf(s + 9, " $%31[^,], %15s", name, regname) != 2)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected LOADQWORD $name, "
                        "<register>\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = find_data(name, data, data_count);
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADQWORD, out);
            fputc(reg, out);
            Data *d = &data[offset];
            uint32_t offset_in_table = d->offset;
            write_u32(out, offset_in_table);
            if (debug)
                printf("[DEBUG] LOADQWORD $%s (offset=%u) -> %s\n", name,
                       offset, regname);
        }
        else if (starts_with_ci(s, "CONTINUE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_CONTINUE, out);
        }
        else if (equals_ci(s, "NEWLINE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_NEWLINE, out);
        }
        else if (s[0] == '.')
        {
            continue;
        }
        else if (starts_with_ci(s, "JE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label[32];
            if (sscanf(s + 2, " %31s", label) != 1)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected JE <label>\nGot: %s\n",
                    lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JE to %s (addr=%u)\n", label, (unsigned)addr);
            }
            fputc(OPCODE_JE, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "JNE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label[32];
            if (sscanf(s + 3, " %31s", label) != 1)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected JNE <label>\nGot: %s\n",
                    lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JNE to %s (addr=%u)\n", label, (unsigned)addr);
            }
            fputc(OPCODE_JNE, out);
            write_u32(out, addr);
        }

        else if (starts_with_ci(s, "HALT"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char token[32];
            if (sscanf(s + 4, " %31s", token) == 1)
            {
                for (int t = 0; token[t]; t++)
                {
                    if (token[t] == '\r' || token[t] == '\n')
                    {
                        token[t] = '\0';
                        break;
                    }
                }
                uint8_t val = 0;
                if (equals_ci(token, "OK"))
                {
                    val = 0;
                }
                else if (equals_ci(token, "BAD"))
                {
                    val = 1;
                }
                else
                {
                    char *endp = NULL;
                    unsigned long v = strtoul(token, &endp, 0);
                    if (endp == NULL || *endp != '\0')
                    {
                        fprintf(stderr,
                                "Syntax error on line %d: invalid HALT operand "
                                "'%s'\nGot: %s\n",
                                lineno, token, s);
                        fclose(in);
                        fclose(out);
                        return 1;
                    }
                    val = (uint8_t)v;
                }
                fputc(OPCODE_HALT, out);
                fputc(val, out);
            }
            else
            {
                fputc(OPCODE_HALT, out);
            }
        }
        else if (starts_with_ci(s, "INC"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 3, " %7s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected INC "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++)
            {
                if (regname[i] == '\r' || regname[i] == '\n')
                {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_INC, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "DEC"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 3, " %7s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected DEC "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++)
            {
                if (regname[i] == '\r' || regname[i] == '\n')
                {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_DEC, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "PRINTREG"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 8, " %7s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected PRINTREG "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++)
            {
                if (regname[i] == '\r' || regname[i] == '\n')
                {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_PRINTREG, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "PRINT_STACKSIZE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_PRINT_STACKSIZE, out);
        }
        else if (starts_with_ci(s, "PRINT"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char c;
            if (sscanf(s + 5, " '%c", &c) != 1)
            {
                fprintf(stderr, "Syntax error on line %d\nGot: %s\n", lineno,
                        line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_PRINT, out);
            fputc((uint8_t)c, out);
        }
        else if (starts_with_ci(s, "JMP"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label_name[32];
            if (sscanf(s + 3, " %31s", label_name) == 0)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected JMP <label>\nGot: %s\n",
                    lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label_name, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JMP to %s (addr=%u)\n", label_name,
                       (unsigned)addr);
            }
            fputc(OPCODE_JMP, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "POP"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            if (sscanf(s + 3, " %3s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected POP "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_POP, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "ADD"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s , %3s", dst_reg, src_reg) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected ADD <src>, "
                        "<dst>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_ADD, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (starts_with_ci(s, "SUB"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s , %3s", dst_reg, src_reg) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected SUB <dst>, "
                        "<src>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_SUB, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (starts_with_ci(s, "MUL"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s, %3s", dst_reg, src_reg) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected MUL <dst>, "
                        "<src>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_MUL, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (starts_with_ci(s, "DIV"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s, %3s", dst_reg, src_reg) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected DIV <dst>, "
                        "<src>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_DIV, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (starts_with_ci(s, "MOV"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char dst_reg[16];
            char src[16];

            if (sscanf(s + 3, " %3s, %15s", dst_reg, src) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected MOV <dst>, "
                        "<src>\nGot: %s\n(If you're using a 2 character "
                        "register like R1 or R2, use R01 or R02 instead!)\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            if (src[0] == 'R')
            {
                uint8_t srcp = parse_register(src, lineno);
                fputc(OPCODE_MOV_REG, out);
                fputc(dst, out);
                fputc(srcp, out);
            }
            else if (src[0] == '\'')
            {
                int32_t imm = (int32_t)(unsigned char)src[1];
                fputc(OPCODE_MOV_IMM, out);
                fputc(dst, out);
                write_u32(out, imm);
            }
            else
            {
                int32_t imm = strtol(src, NULL, 0);
                fputc(OPCODE_MOV_IMM, out);
                fputc(dst, out);
                write_u32(out, imm);
            }
        }
        else if (starts_with_ci(s, "PUSH"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[16];
            if (sscanf(s + 4, " %3s", operand) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected PUSH "
                        "<value|register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (operand[0] == 'R')
            {
                uint8_t reg = parse_register(operand, lineno);
                if (debug)
                {
                    printf("[DEBUG] PUSH_REG\n");
                }
                fputc(OPCODE_PUSH_REG, out);
                fputc(reg, out);
            }
            else
            {
                if (debug)
                {
                    printf("[DEBUG] PUSH_IMM\n");
                }
                char *end;
                int32_t imm = strtol(operand, &end, 0);
                if (*end != '\0')
                {
                    fprintf(stderr, "Invalid immediate on line %d: %s\n",
                            lineno, operand);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                fputc(OPCODE_PUSH_IMM, out);
                write_u32(out, imm);
            }
        }
        else if (starts_with_ci(s, "CMP"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %3s, %3s", reg1, reg2) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected CMP <reg1>, "
                        "<reg2>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t r1 = parse_register(reg1, lineno);
            uint8_t r2 = parse_register(reg2, lineno);
            fputc(OPCODE_CMP, out);
            fputc(r1, out);
            fputc(r2, out);
        }
        else if (starts_with_ci(s, "ALLOC"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 5, " %31s", operand) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected ALLOC "
                        "<elements>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, NULL, 0);
            fputc(OPCODE_ALLOC, out);
            write_u32(out, num);
        }
        else if (starts_with_ci(s, "FRAME"))
        {
            continue;
        }
        else if (starts_with_ci(s, "LOADVAR"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            char addrname[32];
            if (sscanf(s + 7, " %3s, %31s", regname, addrname) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected LOADVAR <register>, "
                        "<slot|Rxx>\nGot: %s\n",
                        lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            if (toupper((unsigned char)addrname[0]) == 'R')
            {
                uint8_t idx = parse_register(addrname, lineno);
                fputc(OPCODE_LOADVAR_REG, out);
                fputc(reg, out);
                fputc(idx, out);
            }
            else
            {
                uint32_t slot = strtoul(addrname, NULL, 0);
                fputc(OPCODE_LOADVAR, out);
                fputc(reg, out);
                write_u32(out, slot);
            }
        }
        else if (starts_with_ci(s, "LOAD"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            char addrname[32];
            if (sscanf(s + 4, " %3s, %31s", regname, addrname) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected LOAD <register>, "
                        "<index in stack|Rxx>\nGot: %s\n",
                        lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            if (toupper((unsigned char)addrname[0]) == 'R')
            {
                uint8_t idx = parse_register(addrname, lineno);
                fputc(OPCODE_LOAD_REG, out);
                fputc(reg, out);
                fputc(idx, out);
            }
            else
            {
                uint32_t addr = strtoul(addrname, NULL, 0);
                fputc(OPCODE_LOAD, out);
                fputc(reg, out);
                write_u32(out, addr);
            }
        }
        else if (starts_with_ci(s, "STOREVAR"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            char addrname[32];
            if (sscanf(s + 8, " %3s, %31s", regname, addrname) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected STOREVAR "
                        "<register>, <slot|Rxx>\nGot: %s\n",
                        lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            if (toupper((unsigned char)addrname[0]) == 'R')
            {
                uint8_t idx = parse_register(addrname, lineno);
                fputc(OPCODE_STOREVAR_REG, out);
                fputc(reg, out);
                fputc(idx, out);
            }
            else
            {
                uint32_t slot = strtoul(addrname, NULL, 0);
                fputc(OPCODE_STOREVAR, out);
                fputc(reg, out);
                write_u32(out, slot);
            }
        }
        else if (starts_with_ci(s, "STORE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            char addrname[32];
            if (sscanf(s + 5, " %3s, %31s", regname, addrname) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected STORE <register>, "
                        "<index in stack|Rxx>\nGot: %s\n",
                        lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            if (toupper((unsigned char)addrname[0]) == 'R')
            {
                uint8_t idx = parse_register(addrname, lineno);
                fputc(OPCODE_STORE_REG, out);
                fputc(reg, out);
                fputc(idx, out);
            }
            else
            {
                uint32_t addr = strtoul(addrname, NULL, 0);
                fputc(OPCODE_STORE, out);
                fputc(reg, out);
                write_u32(out, addr);
            }
        }
        else if (starts_with_ci(s, "GROW"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 4, " %31s", operand) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected GROW <additional "
                        "elements>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, NULL, 0);
            fputc(OPCODE_GROW, out);
            write_u32(out, num);
        }
        else if (starts_with_ci(s, "RESIZE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 6, " %31s", operand) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected RESIZE <new "
                        "size>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, NULL, 0);
            fputc(OPCODE_RESIZE, out);
            write_u32(out, num);
        }
        else if (starts_with_ci(s, "FREE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 4, " %31s", operand) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected FREE <number of "
                        "elements>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, NULL, 0);
            fputc(OPCODE_FREE, out);
            write_u32(out, num);
        }
        else if (starts_with_ci(s, "FOPEN"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char filename[128];
            char mode_raw[8];
            char fid_raw[8];
            if (sscanf(s + 5, " %7[^,], %7[^,], \"%127[^\"]\"", mode_raw,
                       fid_raw, filename) != 3)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected FOPEN <mode>, "
                        "<file_descriptor>, \"<filename>\"\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *mode = trim(mode_raw);
            char *fid = trim(fid_raw);
            uint8_t mode_flag;
            if (equals_ci(mode, "r") == 1)
            {
                mode_flag = 0;
            }
            else if (equals_ci(mode, "w") == 1)
            {
                mode_flag = 1;
            }
            else if (equals_ci(mode, "a") == 1)
            {
                mode_flag = 2;
            }
            else
            {
                fprintf(stderr,
                        "Invalid mode on line %d: %s (expected r, w, or a)\n",
                        lineno, mode);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_FOPEN, out);
            fputc(mode_flag, out);
            uint8_t fd = parse_file(fid, lineno);
            fputc(fd, out);
            uint8_t fname_len = (uint8_t)strlen(filename);
            fputc(fname_len, out);
            for (uint8_t i = 0; i < fname_len; i++)
            {
                fputc((uint8_t)filename[i], out);
            }
        }
        else if (starts_with_ci(s, "FCLOSE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char fid[4];
            if (sscanf(s + 6, " %3s", fid) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected FCLOSE "
                        "<file_descriptor>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t fd = parse_file(fid, lineno);
            fputc(OPCODE_FCLOSE, out);
            fputc(fd, out);
        }
        else if (starts_with_ci(s, "FREAD"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char fid_raw[8];
            char reg_raw[8];
            if (sscanf(s + 5, " %7[^,], %7[^,]", fid_raw, reg_raw) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected FREAD "
                        "<file_descriptor>, <register to read into>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *fid = trim(fid_raw);
            char *reg_tok = trim(reg_raw);
            uint8_t fd = parse_file(fid, lineno);
            uint8_t reg = parse_register(reg_tok, lineno);
            fputc(OPCODE_FREAD, out);
            fputc(fd, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "FSEEK"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char fid_raw[8];
            char offset_raw[64];
            if (sscanf(s + 5, " %7[^,], %63[^\n]", fid_raw, offset_raw) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected FSEEK "
                        "<file_descriptor>, "
                        "<offset_value|offset_register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *fid = trim(fid_raw);
            char *offset_tok = trim(offset_raw);
            uint8_t fd = parse_file(fid, lineno);
            if (offset_tok[0] == 'R')
            {
                uint8_t offset_reg = parse_register(offset_tok, lineno);
                fputc(OPCODE_FSEEK_REG, out);
                fputc(fd, out);
                fputc(offset_reg, out);
            }
            else if (offset_tok[0] == '"')
            {
                char inner[64];
                if (sscanf(offset_tok, "\"%63[^\"]\"", inner) != 1)
                {
                    fprintf(stderr,
                            "Syntax error on line %d: malformed quoted "
                            "offset\nGot: %s\n",
                            lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                int32_t offset_imm = (int32_t)strtol(inner, NULL, 0);
                fputc(OPCODE_FSEEK_IMM, out);
                fputc(fd, out);
                write_u32(out, offset_imm);
            }
            else
            {
                int32_t offset_imm = (int32_t)strtol(offset_tok, NULL, 0);
                fputc(OPCODE_FSEEK_IMM, out);
                fputc(fd, out);
                write_u32(out, offset_imm);
            }
        }
        else if (starts_with_ci(s, "FWRITE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char fid_raw[8];
            char size_raw[64];
            if (sscanf(s + 6, " %7[^,], %63[^\n]", fid_raw, size_raw) != 2)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected FWRITE "
                    "<file_descriptor>, <size_value|size_register>\nGot: %s\n",
                    lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *fid = trim(fid_raw);
            char *size_tok = trim(size_raw);
            uint8_t fd = parse_file(fid, lineno);
            if (size_tok[0] == 'R')
            {
                uint8_t size_reg = parse_register(size_tok, lineno);
                fputc(OPCODE_FWRITE_REG, out);
                fputc(fd, out);
                fputc(size_reg, out);
            }
            else if (size_tok[0] == '"')
            {
                char inner[64];
                if (sscanf(size_tok, "\"%63[^\"]\"", inner) != 1)
                {
                    fprintf(stderr,
                            "Syntax error on line %d: malformed quoted "
                            "size\nGot: %s\n",
                            lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                uint32_t size_imm = strtoul(inner, NULL, 0);
                fputc(OPCODE_FWRITE_IMM, out);
                fputc(fd, out);
                write_u32(out, size_imm);
            }
            else
            {
                uint32_t size_imm = strtoul(size_tok, NULL, 0);
                fputc(OPCODE_FWRITE_IMM, out);
                fputc(fd, out);
                write_u32(out, size_imm);
            }
        }
        else if (starts_with_ci(s, "NOT"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            if (sscanf(s + 3, " %3s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected NOT "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_NOT, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "AND"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %3s, %3s", reg1, reg2) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected AND <reg1>, "
                        "<reg2>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t r1 = parse_register(reg1, lineno);
            uint8_t r2 = parse_register(reg2, lineno);
            fputc(OPCODE_AND, out);
            fputc(r1, out);
            fputc(r2, out);
        }
        else if (starts_with_ci(s, "OR"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 2, " %3s, %3s", reg1, reg2) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected OR <reg1>, "
                        "<reg2>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t r1 = parse_register(reg1, lineno);
            uint8_t r2 = parse_register(reg2, lineno);
            fputc(OPCODE_OR, out);
            fputc(r1, out);
            fputc(r2, out);
        }
        else if (starts_with_ci(s, "XOR"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %3s, %3s", reg1, reg2) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected XOR <reg1>, "
                        "<reg2>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t r1 = parse_register(reg1, lineno);
            uint8_t r2 = parse_register(reg2, lineno);
            fputc(OPCODE_XOR, out);
            fputc(r1, out);
            fputc(r2, out);
        }
        else if (starts_with_ci(s, "READCHAR"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 8, " %7s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected READCHAR "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++)
            {
                if (regname[i] == '\r' || regname[i] == '\n')
                {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_READCHAR, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "READSTR"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 7, " %7s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected READSTR "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++)
            {
                if (regname[i] == '\r' || regname[i] == '\n')
                {
                    regname[i] = '\0';
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_READSTR, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "SLEEP"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char operand[32];
            if (sscanf(s + 5, " %31s", operand) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected SLEEP "
                        "<milliseconds>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t ms = strtoul(operand, NULL, 0);
            fputc(OPCODE_SLEEP, out);
            write_u32(out, ms);
        }
        else if (starts_with_ci(s, "CLRSCR"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_CLRSCR, out);
        }
        else if (starts_with_ci(s, "RAND"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];
            char rest[64] = {0};
            // RAND <reg>
            // RAND <reg>, <max>
            // RAND <reg>, <min>, <max>
            int matched = sscanf(s + 4, " %3s , %63[^\n]", regname, rest);
            if (matched < 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected RAND <register>[, "
                        "<max>] or RAND <register>[, <min>, <max>]\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_RAND, out);
            fputc(reg, out);

            if (matched == 2 && rest[0] != '\0')
            {
                char a[32] = {0}, b[32] = {0};
                int cnt = sscanf(rest, " %31[^,] , %31s", a, b);
                if (cnt == 2)
                {
                    int32_t min = (int32_t)strtol(trim(a), NULL, 0);
                    int32_t max = (int32_t)strtol(trim(b), NULL, 0);
                    write_u64(out, (uint64_t)min);
                    write_u64(out, (uint64_t)max);
                }
                else
                {
                    write_u64(out, (uint64_t)INT64_MIN);
                    write_u64(out, (uint64_t)INT64_MAX);
                }
            }
            else
            {
                write_u64(out, (uint64_t)INT64_MIN);
                write_u64(out, (uint64_t)INT64_MAX);
            }
        }
        else if (starts_with_ci(s, "GETKEY"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 6, " %7s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected GETKEY "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_GETKEY, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "READ"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char regname[16];

            if (sscanf(s + 4, " %7s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected READ "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_READ, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "JL"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label[32];
            if (sscanf(s + 3, " %31s", label) != 1)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected JL <label>\nGot: %s\n",
                    lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JL to %s (addr=%u)\n", label, (unsigned)addr);
            }
            fputc(OPCODE_JL, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "JGE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label[32];
            if (sscanf(s + 3, " %31s", label) != 1)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected JGE <label>\nGot: %s\n",
                    lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JGE to %s (addr=%u)\n", label, (unsigned)addr);
            }
            fputc(OPCODE_JGE, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "JB"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label[32];
            if (sscanf(s + 2, " %31s", label) != 1)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected JB <label>\nGot: %s\n",
                    lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JB to %s (addr=%u)\n", label, (unsigned)addr);
            }
            fputc(OPCODE_JB, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "JAE"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label[32];
            if (sscanf(s + 3, " %31s", label) != 1)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected JAE <label>\nGot: %s\n",
                    lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label, labels, label_count);
            if (debug)
            {
                printf("[DEBUG] JAE to %s (addr=%u)\n", label, (unsigned)addr);
            }
            fputc(OPCODE_JAE, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "CALL"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char label[32];
            if (sscanf(s + 4, " %31s", label) != 1)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected CALL <label>\nGot: %s\n",
                    lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *comma = strchr(s + 4, ',');
            uint32_t frame_size = 0;
            if (comma)
            {
                frame_size = (uint32_t)strtoul(comma + 1, NULL, 0);
            }
            uint32_t addr = find_label(label, labels, label_count);
            if (!comma)
            {
                for (size_t i = 0; i < label_count; i++)
                {
                    if (strcmp(labels[i].name, label) == 0)
                    {
                        frame_size = labels[i].frame_size;
                        break;
                    }
                }
            }
            if (debug)
            {
                printf("[DEBUG] CALL to %s (addr=%u frame=%u)\n", label,
                       (unsigned)addr, (unsigned)frame_size);
            }
            fputc(OPCODE_CALL, out);
            write_u32(out, addr);
            write_u32(out, frame_size);
        }
        else if (starts_with_ci(s, "RET"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_RET, out);
        }
        else if (starts_with_ci(s, "MOD"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s, %3s", dst_reg, src_reg) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected MOD <dst>, "
                        "<src>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_MOD, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (starts_with_ci(s, "BREAK"))
        {
            if (debug)
            {
                printf("[DEBUG] Encoding instruction: %s\n", s);
            }
            fputc(OPCODE_BREAK, out);
        }
        else
        {
            fprintf(stderr, "Unknown instruction on line %d:\n %s\n", lineno,
                    s);
            fclose(in);
            fclose(out);
            return 1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}