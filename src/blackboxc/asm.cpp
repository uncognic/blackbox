#include "asm.h"
#include "../define.h"
#include "tools.h"
#include "asm_util.h"
#include "../data.h"

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

int assemble_file(const char *filename, const char *output_file, int debug)
{
    FILE *in = NULL;
    {
        using FilePtr = std::unique_ptr<FILE, int (*)(FILE *)>;
        std::vector<std::string> lines;
        std::string preprocessed;
        if (!preprocess_includes(filename, preprocessed))
        {
            return 1;
        }

        if (bbxc::asm_helpers::collect_lines_from_buffer(preprocessed.c_str(), lines) != 0)
        {
            return 1;
        }

        FilePtr tmp(tmpfile(), fclose);
        if (!tmp)
        {
            perror("tmpfile");
            return 1;
        }

        std::vector<Macro> macros;

        for (size_t i = 0; i < lines.size(); i++)
        {
            std::string trimmed = bbxc::asm_helpers::trim_copy(lines[i]);
            if (starts_with_ci(trimmed.c_str(), "%macro") &&
                trimmed.size() > 6 &&
                (trimmed[6] == ' ' || trimmed[6] == '\t'))
            {
                std::string header = bbxc::asm_helpers::trim_copy(trimmed.substr(6));
                std::vector<std::string> header_tokens = bbxc::asm_helpers::split_tokens(header);
                if (header_tokens.empty())
                {
                    fprintf(stderr, "Syntax error: bad %%macro header\n");
                    continue;
                }

                std::vector<std::string> params;
                params.reserve(header_tokens.size() > 1 ? header_tokens.size() - 1 : 0);
                for (size_t p = 1; p < header_tokens.size(); p++)
                {
                    params.push_back(header_tokens[p]);
                }

                std::vector<std::string> body;
                size_t j = i + 1;
                for (; j < lines.size(); j++)
                {
                    std::string line_trimmed = bbxc::asm_helpers::trim_copy(lines[j]);
                    if (equals_ci(line_trimmed.c_str(), "%endmacro"))
                    {
                        break;
                    }
                    body.push_back(lines[j]);
                }

                Macro macro = {};
                if (!bbxc::asm_helpers::build_macro_owned(header_tokens[0], params, body, macro))
                {
                    fprintf(stderr, "Out of memory\n");
                    return 1;
                }
                macros.push_back(macro);

                i = j;
            }
        }

        unsigned long expand_id = 0;

        for (size_t i = 0; i < lines.size(); i++)
        {
            std::string trimmed = bbxc::asm_helpers::trim_copy(lines[i]);
            if (starts_with_ci(trimmed.c_str(), "%macro"))
            {
                size_t j = i + 1;
                for (; j < lines.size(); j++)
                {
                    std::string line_trimmed = bbxc::asm_helpers::trim_copy(lines[j]);
                    if (equals_ci(line_trimmed.c_str(), "%endmacro"))
                    {
                        break;
                    }
                }
                i = j;
                continue;
            }
            if (!trimmed.empty() && trimmed[0] == '%')
            {
                if (equals_ci(trimmed.c_str(), "%asm") ||
                    equals_ci(trimmed.c_str(), "%data") ||
                    equals_ci(trimmed.c_str(), "%main") ||
                    equals_ci(trimmed.c_str(), "%entry") ||
                    equals_ci(trimmed.c_str(), "%endmacro"))
                {
                    fputs(lines[i].c_str(), tmp.get());
                    continue;
                }
                if (expand_invocation(trimmed.c_str(), tmp.get(), 0,
                                      macros.data(), macros.size(),
                                      &expand_id))
                {
                    continue;
                }
            }
            fputs(lines[i].c_str(), tmp.get());
        }

        for (size_t m = 0; m < macros.size(); m++)
        {
            bbxc::asm_helpers::free_macro_owned(macros[m]);
        }

        rewind(tmp.get());
        in = tmp.release();
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
            return bbxc::asm_helpers::failf(in, out, "Error: file must start with %%asm (line %d)", lineno);
        }
    }

    std::vector<Label> labels;
    labels.reserve(256);
    uint32_t pc = MAGIC_SIZE;

    std::vector<Data> data;
    data.reserve(256);
    uint32_t data_table_size = 0;
    std::vector<uint8_t> data_table;
    data_table.reserve(8192);

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
                return bbxc::asm_helpers::failf(in, out,
                             "Error on line %d: %%data section must come before %%main/%%entry",
                             lineno);
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
                return bbxc::asm_helpers::failf(in, out,
                             "Error on line %d: STR must be inside %%data section",
                             lineno);
            }
            char name[32];
            char *quote_start = strchr(s, '"');
            if (!quote_start || sscanf(s + 4, " $%31[^,]", name) != 1)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Syntax error line %d: expected STR $name, \"value\"",
                             lineno);
            }

            quote_start++;
            char *quote_end = strchr(quote_start, '"');
            if (!quote_end)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Syntax error line %d: missing closing quote",
                             lineno);
            }
            size_t len = quote_end - quote_start;

            if (data.size() >= 256)
            {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            data_table.insert(data_table.end(), quote_start, quote_start + len);
            data_table.push_back('\0');
            bbxc::asm_helpers::append_data_item(data, name, DATA_STRING, data_table_size, 0);
            data_table_size += (uint32_t)(len + 1);
            if (debug)
                printf("[DEBUG] String $%s at offset %u\n", name,
                       data.back().offset);
            continue;
        }
        else if (starts_with_ci(s, "DWORD "))
        {
            if (current_section != 1)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Error on line %d: DWORD must be inside %%data section",
                             lineno);
            }
            char name[32];
            uint32_t value;
            if (sscanf(s + 6, " $%31[^,], %u", name, &value) != 2)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Syntax error line %d: expected DWORD $name, value",
                             lineno);
            }

            if (data.size() >= 256)
            {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            bbxc::asm_helpers::append_le_bytes(data_table, value, sizeof(value));
            bbxc::asm_helpers::append_data_item(data, name, DATA_DWORD, data_table_size, value);
            data_table_size += (uint32_t)sizeof(value);
            if (debug)
                printf("[DEBUG] DWORD $%s at offset %u\n", name,
                       data.back().offset);
            continue;
        }
        else if (starts_with_ci(s, "QWORD "))
        {
            if (current_section != 1)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Error on line %d: QWORD must be inside %%data section",
                             lineno);
            }
            char name[32];
            char value_str[64];
            uint64_t value;
            if (sscanf(s + 6, " $%31[^,], %63s", name, value_str) != 2)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Syntax error line %d: expected QWORD $name, value",
                             lineno);
            }
            value = (uint64_t)strtoull(value_str, NULL, 0);

            if (data.size() >= 256)
            {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            bbxc::asm_helpers::append_le_bytes(data_table, value, sizeof(value));
            bbxc::asm_helpers::append_data_item(data, name, DATA_QWORD, data_table_size, value);
            data_table_size += (uint32_t)sizeof(value);
            if (debug)
                printf("[DEBUG] QWORD $%s at offset %u\n", name,
                       data.back().offset);
            continue;
        }
        else if (starts_with_ci(s, "WORD "))
        {
            if (current_section != 1)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Error on line %d: WORD must be inside %%data section",
                             lineno);
            }
            char name[32];
            uint16_t value;
            if (sscanf(s + 5, " $%31[^,], %hu", name, &value) != 2)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Syntax error line %d: expected WORD $name, value",
                             lineno);
            }

            if (data.size() >= 256)
            {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            bbxc::asm_helpers::append_le_bytes(data_table, value, sizeof(value));
            bbxc::asm_helpers::append_data_item(data, name, DATA_WORD, data_table_size, value);
            data_table_size += (uint32_t)sizeof(value);
            if (debug)
                printf("[DEBUG] WORD $%s at offset %u\n", name,
                       data.back().offset);
            continue;
        }
        else if (starts_with_ci(s, "BYTE "))
        {
            if (current_section != 1)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Error on line %d: BYTE must be inside %%data section",
                             lineno);
            }
            char name[32];
            uint8_t value;
            if (sscanf(s + 5, " $%31[^,], %hhu", name, &value) != 2)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Syntax error line %d: expected BYTE $name, value",
                             lineno);
            }

            if (data.size() >= 256)
            {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            data_table.push_back(value);
            bbxc::asm_helpers::append_data_item(data, name, DATA_BYTE, data_table_size, value);
            data_table_size += (uint32_t)sizeof(value);
            if (debug)
                printf("[DEBUG] BYTE $%s at offset %u\n", name,
                       data.back().offset);
            continue;
        }

        if (current_section != 2)
        {
            size_t len = strlen(s);
            if (s[0] == '.' && s[len - 1] == ':')
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Error on line %d: labels must be inside %%main/%%entry section",
                             lineno);
            }
            if (current_section == 0)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Error on line %d: code outside of section. Use %%string or %%main/%%entry",
                             lineno);
            }
            continue;
        }

        size_t len = strlen(s);
        if (s[0] == '.' && s[len - 1] == ':')
        {
            if (labels.size() >= 256)
            {
                return bbxc::asm_helpers::failf(in, out, "Too many labels (max 256)");
            }

            s[len - 1] = '\0';
            Label label = {};
            strncpy(label.name, s + 1, sizeof(label.name) - 1);
            label.name[sizeof(label.name) - 1] = '\0';
            label.addr = pc;
            label.frame_size = 0;
            labels.push_back(label);
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Label %s at pc=%u\n", labels.back().name,
                (unsigned)labels.back().addr);
            continue;
        }

        if (starts_with_ci(s, "frame"))
        {
            if (labels.empty())
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Error on line %d: FRAME must follow a label",
                             lineno);
            }
            char framebuf[32];
            if (sscanf(s + 5, " %31s", framebuf) != 1)
            {
                return bbxc::asm_helpers::failf(in, out,
                             "Syntax error on line %d: expected FRAME <slots>",
                             lineno);
            }
            uint32_t fs = (uint32_t)strtoul(framebuf, NULL, 0);
            labels.back().frame_size = fs;
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
    for (size_t i = 0; i < labels.size(); i++)
    {
        labels[i].addr += header_offset;
    }

    rewind(in);
    lineno = 0;
    current_section = 0;

    fputc((MAGIC >> 16) & 0xFF, out);
    fputc((MAGIC >> 8) & 0xFF, out);
    fputc((MAGIC >> 0) & 0xFF, out);
    fputc((uint8_t)data.size(), out);
    write_u32(out, data_table_size);
    if (data_table_size > 0)
        fwrite(data_table.data(), 1, data_table_size, out);

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
        else if (starts_with_ci(s, "write"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char fd_str[16];
            char *str_start;

            if (sscanf(s + 5, " %15[^, \t]", fd_str) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected WRITE <STDOUT|STDERR> "
                        "\"<string>\"\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t fd;
            if (equals_ci(fd_str, "STDOUT"))
            {
                fd = 1;
            }
            else if (equals_ci(fd_str, "STDERR"))
            {
                fd = 2;
            }
            else
            {
                fprintf(stderr,
                        "Invalid file descriptor on line %d: %s (expected "
                        "STDOUT or stderr)\n",
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
        else if (starts_with_ci(s, "loadstr"))
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
            uint32_t offset = find_data(name, data.data(), data.size());
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADSTR, out);
            fputc(reg, out);
            uint32_t mem_offset = data[offset].offset;
            write_u32(out, mem_offset);
            if (debug)
                printf("[DEBUG] LOADSTR $%s (offset=%u) -> %s\n", name, offset,
                       regname);
        }
        else if (starts_with_ci(s, "printstr"))
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
        else if (starts_with_ci(s, "eprintstr"))
        {
            if (debug)
                printf("[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            if (sscanf(s + 9, " %15s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error line %d: expected EPRINTSTR <register>\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_EPRINTSTR, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "loadbyte"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t offset = find_data(name, data.data(), data.size());
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADBYTE, out);
            fputc(reg, out);
            Data *d = &data[offset];
            if (d->type != DATA_BYTE)
            {
                fprintf(stderr, "Warning line %d: LOADBYTE used on $%s which is not a BYTE\n", lineno, name);
            }
            uint32_t offset_in_table = d->offset;
            write_u32(out, offset_in_table);
            if (debug)
                printf("[DEBUG] LOADBYTE $%s (offset=%u) -> %s\n", name, offset,
                       regname);
        }
        else if (starts_with_ci(s, "loadword"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t offset = find_data(name, data.data(), data.size());
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADWORD, out);
            fputc(reg, out);
            Data *d = &data[offset];
            if (d->type != DATA_WORD)
            {
                fprintf(stderr, "Warning line %d: LOADWORD used on $%s which is not a WORD\n", lineno, name);
            }
            uint32_t offset_in_table = d->offset;
            write_u32(out, offset_in_table);
            if (debug)
                printf("[DEBUG] LOADWORD $%s (offset=%u) -> %s\n", name, offset,
                       regname);
        }
        else if (starts_with_ci(s, "loaddword"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t offset = find_data(name, data.data(), data.size());
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADDWORD, out);
            fputc(reg, out);
            Data *d = &data[offset];
            if (d->type != DATA_DWORD)
            {
                fprintf(stderr, "Warning line %d: LOADDWORD used on $%s which is not a DWORD\n", lineno, name);
            }
            uint32_t offset_in_table = d->offset;
            write_u32(out, offset_in_table);
            if (debug)
                printf("[DEBUG] LOADDWORD $%s (offset=%u) -> %s\n", name,
                       offset, regname);
        }
        else if (starts_with_ci(s, "loadqword"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t offset = find_data(name, data.data(), data.size());
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_LOADQWORD, out);
            fputc(reg, out);
            Data *d = &data[offset];
            if (d->type != DATA_QWORD)
            {
                fprintf(stderr, "Warning line %d: LOADQWORD used on $%s which is not a QWORD\n", lineno, name);
            }
            uint32_t offset_in_table = d->offset;
            write_u32(out, offset_in_table);
            if (debug)
                printf("[DEBUG] LOADQWORD $%s (offset=%u) -> %s\n", name,
                       offset, regname);
        }
        else if (starts_with_ci(s, "continue"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(OPCODE_CONTINUE, out);
        }
        else if (equals_ci(s, "newline"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(OPCODE_NEWLINE, out);
        }
        else if (s[0] == '.')
        {
            continue;
        }
        else if (starts_with_ci(s, "je"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t addr = find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JE to %s (addr=%u)\n", label, (unsigned)addr);
            fputc(OPCODE_JE, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "jne"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t addr = find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JNE to %s (addr=%u)\n", label, (unsigned)addr);
            fputc(OPCODE_JNE, out);
            write_u32(out, addr);
        }

        else if (starts_with_ci(s, "halt"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char token[32];
            if (sscanf(s + 4, " %31s", token) == 1)
            {
                for (int t = 0; token[t]; t++)
                {
                    if (token[t] == '\r' || token[t] == '\n')
                    {
                        bbxc::asm_helpers::trim_crlf(token);
                        break;
                    }
                }
                uint8_t val = 0;
                if (equals_ci(token, "ok"))
                {
                    val = 0;
                }
                else if (equals_ci(token, "bad"))
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
        else if (starts_with_ci(s, "inc"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_INC, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "dec"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_DEC, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "printreg"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_PRINTREG, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "eprintreg"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];

            if (sscanf(s + 9, " %7s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected EPRINTREG "
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
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_EPRINTREG, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "print_stacksize"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(OPCODE_PRINT_STACKSIZE, out);
        }
        else if (starts_with_ci(s, "PRINTCHAR"))
        {
            char reg[16];
            if (sscanf(s + 9, " %15s", reg) != 1)
            {
                fprintf(stderr, "Syntax error line %d: expected PRINTCHAR <register>\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_PRINTCHAR, out);
            fputc(parse_register(reg, lineno), out);
        }
        else if (starts_with_ci(s, "EPRINTCHAR"))
        {
            char reg[16];
            if (sscanf(s + 10, " %15s", reg) != 1)
            {
                fprintf(stderr, "Syntax error line %d: expected EPRINTCHAR <register>\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_EPRINTCHAR, out);
            fputc(parse_register(reg, lineno), out);
        }
        else if (starts_with_ci(s, "print"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "jmpi"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            uint32_t addr;
            if (sscanf(s + 5, " %u", &addr) != 1)
            {
                fprintf(
                    stderr,
                    "Syntax error on line %d: expected JMPI <addr>\nGot: %s\n",
                    lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JMPI to addr %u\n", addr);
            fputc(OPCODE_JMPI, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "jmp"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t addr = find_label(label_name, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JMP to %s (addr=%u)\n", label_name,
                (unsigned)addr);
            fputc(OPCODE_JMP, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "pop"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "add"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "sub"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "mul"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "div"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "movi"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char dst_reg[16];
            char src[16];

            if (sscanf(s + 4, " %3s, %15s", dst_reg, src) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected MOVI <dst>, "
                        "<value>\nGot: %s\n(If you're using a 2 character "
                        "register like R1 or R2, use R01 or R02 instead!)\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = parse_register(dst_reg, lineno);
            if (src[0] == '\'')
            {
                int32_t imm = (int32_t)(unsigned char)src[1];
                fputc(OPCODE_MOVI, out);
                fputc(dst, out);
                write_u32(out, imm);
            }
            else
            {
                int32_t imm = strtol(src, NULL, 0);
                fputc(OPCODE_MOVI, out);
                fputc(dst, out);
                write_u32(out, imm);
            }
        }
        else if (starts_with_ci(s, "mov"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char dst_reg[16];
            char src_reg[16];

            if (sscanf(s + 3, " %15[^,], %15s", dst_reg, src_reg) != 2)
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
            uint8_t src = parse_register(src_reg, lineno);
            fputc(OPCODE_MOV_REG, out);
            fputc(dst, out);
            fputc(src, out);
        }
        else if (starts_with_ci(s, "push "))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            if (sscanf(s + 5, " %3s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected PUSH "
                        "<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = parse_register(regname, lineno);
            fputc(OPCODE_PUSH_REG, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "pushi "))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char operand[16];
            if (sscanf(s + 6, " %15s", operand) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected pushi "
                        "<value|register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            bbxc::asm_helpers::dbg(debug, "[DEBUG] pushi %s\n", operand);
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
            fputc(OPCODE_PUSHI, out);
            write_u32(out, imm);
        }

        else if (starts_with_ci(s, "cmp"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "alloc"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "frame"))
        {
            continue;
        }
        else if (starts_with_ci(s, "loadvar"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "load"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "storevar"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "store"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "grow"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "resize"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "free"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "fopen"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "fclose"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "fread"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "fseek"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_READCHAR, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "READSTR"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = parse_register(regname, lineno);

            fputc(OPCODE_READSTR, out);
            fputc(reg, out);
        }
        else if (starts_with_ci(s, "SLEEP"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char operand[64];
            if (sscanf(s + 5, " %63s", operand) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected SLEEP "
                        "<milliseconds>|<register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }

            if (toupper((unsigned char)operand[0]) == 'R')
            {
                uint8_t reg = parse_register(operand, lineno);
                fputc(OPCODE_SLEEP_REG, out);
                fputc(reg, out);
            }
            else
            {
                uint32_t ms = (uint32_t)strtoul(operand, NULL, 0);
                fputc(OPCODE_SLEEP, out);
                write_u32(out, ms);
            }
        }
        else if (starts_with_ci(s, "clrscr"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(OPCODE_CLRSCR, out);
        }
        else if (starts_with_ci(s, "rand"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "getkey"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
        else if (starts_with_ci(s, "read"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t addr = find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JL to %s (addr=%u)\n", label, (unsigned)addr);
            fputc(OPCODE_JL, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "JGE"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t addr = find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JGE to %s (addr=%u)\n", label, (unsigned)addr);
            fputc(OPCODE_JGE, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "JB"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t addr = find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JB to %s (addr=%u)\n", label, (unsigned)addr);
            fputc(OPCODE_JB, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "JAE"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t addr = find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JAE to %s (addr=%u)\n", label, (unsigned)addr);
            fputc(OPCODE_JAE, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "CALL"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            uint32_t addr = find_label(label, labels.data(), labels.size());
            if (!comma)
            {
                for (size_t i = 0; i < labels.size(); i++)
                {
                    if (strcmp(labels[i].name, label) == 0)
                    {
                        frame_size = labels[i].frame_size;
                        break;
                    }
                }
            }
            bbxc::asm_helpers::dbg(debug, "[DEBUG] CALL to %s (addr=%u frame=%u)\n", label,
                (unsigned)addr, (unsigned)frame_size);
            fputc(OPCODE_CALL, out);
            write_u32(out, addr);
            write_u32(out, frame_size);
        }
        else if (starts_with_ci(s, "RET"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(OPCODE_RET, out);
        }
        else if (starts_with_ci(s, "MOD"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
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
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(OPCODE_BREAK, out);
        }
        else if (starts_with_ci(s, "EXEC"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char *str_start = strchr(s, '"');
            if (!str_start)
            {
                fprintf(stderr,
                        "Syntax error on line %d: missing opening quote for EXEC\n",
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
                        "Syntax error on line %d: missing closing quote for EXEC\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }

            size_t len = (size_t)(str_end - str_start);
            if (len > 255)
                len = 255;

            char *after = str_end + 1;
            while (*after == ' ' || *after == '\t' || *after == ',')
                after++;
            char regname[16];
            if (sscanf(after, " %15s", regname) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected EXEC \"<cmd>\", <register>\n",
                        lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++)
            {
                if (regname[i] == '\r' || regname[i] == '\n')
                {
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t dest_reg = parse_register(regname, lineno);

            fputc(OPCODE_EXEC, out);
            fputc(dest_reg, out);
            fputc((uint8_t)len, out);
            for (size_t i = 0; i < len; i++)
            {
                fputc((uint8_t)str_start[i], out);
            }
            bbxc::asm_helpers::dbg(debug, "[DEBUG] EXEC -> %.*s (dest=%s)\n", (int)len, str_start, regname);
        }
        else if (starts_with_ci(s, "REGSYSCALL"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            uint8_t id;
            char label[32];
            if (sscanf(s + 10, " %hhu, %31s", &id, label) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected REGSYSCALL <id>, <label>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label, labels.data(), labels.size());
            fputc(OPCODE_REGSYSCALL, out);
            fputc(id, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "SYSCALL"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            uint8_t id;
            if (sscanf(s + 7, " %hhu", &id) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected SYSCALL <id>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_SYSCALL, out);
            fputc(id, out);
        }
        else if (starts_with_ci(s, "SYSRET"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(OPCODE_SYSRET, out);
        }
        else if (starts_with_ci(s, "DROPPRIV"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(OPCODE_DROPPRIV, out);
        }
        else if (starts_with_ci(s, "GETMODE"))
        {
            char reg[16];
            if (sscanf(s + 7, " %15s", reg) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected GETMODE <register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_GETMODE, out);
            fputc(parse_register(reg, lineno), out);
        }
        else if (starts_with_ci(s, "SETPERM"))
        {
            uint32_t start, count;
            char perm_str[16];
            if (sscanf(s + 7, " %u, %u, %15s", &start, &count, perm_str) != 3)
            {
                fprintf(stderr, "Syntax error on line %d: expected SETPERM <start>, <count>, <priv>/<prot>\nGot: %s\n", lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }

            char *slash = strchr(perm_str, '/');
            if (!slash)
            {
                fprintf(stderr, "Syntax error on line %d: SETPERM permissions must be <priv>/<prot> e.g. rw/r\nGot: %s\n", lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }

            *slash = '\0';
            char *priv_str = perm_str;
            char *prot_str = slash + 1;

            uint8_t priv_read = strchr(priv_str, 'R') ? 1 : 0;
            uint8_t priv_write = strchr(priv_str, 'W') ? 1 : 0;
            uint8_t prot_read = strchr(prot_str, 'R') ? 1 : 0;
            uint8_t prot_write = strchr(prot_str, 'W') ? 1 : 0;

            fputc(OPCODE_SETPERM, out);
            write_u32(out, start);
            write_u32(out, count);
            fputc(priv_read, out);
            fputc(priv_write, out);
            fputc(prot_read, out);
            fputc(prot_write, out);
        }
        else if (starts_with_ci(s, "DUMPREGS"))
        {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(OPCODE_DUMPREGS, out);
        }
        else if (starts_with_ci(s, "REGFAULT"))
        {
            uint8_t id;
            char label[32];
            if (sscanf(s + 8, " %hhu, %31s", &id, label) != 2)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected REGFAULT <id>, <label>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = find_label(label, labels.data(), labels.size());
            fputc(OPCODE_REGFAULT, out);
            fputc(id, out);
            write_u32(out, addr);
        }
        else if (starts_with_ci(s, "FAULTRET"))
        {
            fputc(OPCODE_FAULTRET, out);
        }
        else if (starts_with_ci(s, "GETFAULT"))
        {
            char reg[16];
            if (sscanf(s + 8, " %15s", reg) != 1)
            {
                fprintf(stderr, "Syntax error line %d: expected GETFAULT <register>\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_GETFAULT, out);
            fputc(parse_register(reg, lineno), out);
        }
        else if (starts_with_ci(s, "SHLI"))
        {
            char reg[16];
            char amount[32];
            char *comma = strchr(s + 4, ',');
            if (!comma || sscanf(s + 4, " %15[^,]", reg) != 1 ||
                sscanf(comma + 1, " %31s", amount) != 1)
            {
                fprintf(stderr, "Syntax error line %d: expected SHLI <register>, <shift>\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (amount[0] == '-')
            {
                fprintf(stderr, "Syntax error line %d: shift count must be non-negative\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *end;
            uint64_t shift = strtoull(amount, &end, 0);
            if (*end != '\0')
            {
                fprintf(stderr, "Syntax error line %d: invalid shift count '%s'\n", lineno, amount);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_SHLI, out);
            fputc(parse_register(reg, lineno), out);
            write_u64(out, (uint64_t)shift);
        }
        else if (starts_with_ci(s, "SHRI"))
        {
            char reg[16];
            char amount[32];
            char *comma = strchr(s + 4, ',');
            if (!comma || sscanf(s + 4, " %15[^,]", reg) != 1 ||
                sscanf(comma + 1, " %31s", amount) != 1)
            {
                fprintf(stderr, "Syntax error line %d: expected SHRI <register>, <shift>\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (amount[0] == '-')
            {
                fprintf(stderr, "Syntax error line %d: shift count must be non-negative\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *end;
            uint64_t shift = strtoull(amount, &end, 0);
            if (*end != '\0')
            {
                fprintf(stderr, "Syntax error line %d: invalid shift count '%s'\n", lineno, amount);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_SHRI, out);
            fputc(parse_register(reg, lineno), out);
            write_u64(out, (uint64_t)shift);
        }
        else if (starts_with_ci(s, "SHL"))
        {
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %15[^,], %15s", reg1, reg2) != 2)
            {
                fprintf(stderr, "Syntax error line %d: expected SHL <dst>, <src>\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_SHL, out);
            fputc(parse_register(reg1, lineno), out);
            fputc(parse_register(reg2, lineno), out);
        }
        else if (starts_with_ci(s, "SHR"))
        {
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %15[^,], %15s", reg1, reg2) != 2)
            {
                fprintf(stderr, "Syntax error line %d: expected SHR <dst>, <src>\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_SHR, out);
            fputc(parse_register(reg1, lineno), out);
            fputc(parse_register(reg2, lineno), out);
        }
        else if (starts_with_ci(s, "GETARGC"))
        {
            char reg[16];
            if (sscanf(s + 7, " %15s", reg) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected GETARGC <register>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_GETARGC, out);
            fputc(parse_register(reg, lineno), out);
        }
        else if (starts_with_ci(s, "GETARG"))
        {
            char reg[16];
            char index[32];
            char *comma = strchr(s + 7, ',');
            if (!comma || sscanf(s + 7, " %15[^,]", reg) != 1 ||
                sscanf(comma + 1, " %31s", index) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected GETARG <register>, <index>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char *end;
            uint32_t idx = strtoul(index, &end, 0);
            if (*end != '\0')
            {
                fprintf(stderr,
                        "Syntax error on line %d: invalid argument index '%s'\nGot: %s\n",
                        lineno, index, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_GETARG, out);
            fputc(parse_register(reg, lineno), out);
            write_u32(out, idx);
        }
        else if (starts_with_ci(s, "GETENV"))
        {
            char reg[16];
            char varname[256];
            char *comma = strchr(s + 6, ',');
            if (!comma || sscanf(s + 6, " %15[^,]", reg) != 1)
            {
                fprintf(stderr,
                        "Syntax error on line %d: expected GETENV <register>, <varname>\nGot: %s\n",
                        lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }

            const char *p = comma + 1;
            while (*p && isspace((unsigned char)*p))
                p++;

            if (*p == '"')
            {
                const char *end = strchr(p + 1, '"');
                if (!end)
                {
                    fprintf(stderr,
                            "Syntax error on line %d: expected GETENV <register>, \"<varname>\"\nGot: %s\n",
                            lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                size_t len = (size_t)(end - (p + 1));
                if (len >= sizeof(varname))
                    len = sizeof(varname) - 1;
                memcpy(varname, p + 1, len);
                varname[len] = '\0';
            }
            else
            {
                if (sscanf(p, " %255s", varname) != 1)
                {
                    fprintf(stderr,
                            "Syntax error on line %d: expected GETENV <register>, <varname>\nGot: %s\n",
                            lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                char *end = varname + strlen(varname) - 1;
                while (end > varname && (*end == '\r' || *end == '\n'))
                {
                    bbxc::asm_helpers::trim_crlf(varname);
                    break;
                }
            }

            fputc(OPCODE_GETENV, out);
            fputc(parse_register(reg, lineno), out);
            uint8_t len = (uint8_t)strlen(varname);
            fputc(len, out);
            for (size_t i = 0; i < len; i++)
            {
                fputc((uint8_t)varname[i], out);
            }
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
