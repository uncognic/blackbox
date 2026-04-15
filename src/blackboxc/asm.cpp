#include "asm.hpp"
#include "../define.hpp"
#include "../utils/asm_parser.hpp"
#include "../utils/data.hpp"
#include "../utils/macro_expansion.hpp"
#include "../utils/preprocessor.hpp"
#include "../utils/string_utils.hpp"
#include "../utils/symbol_table.hpp"
#include "asm_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <print>
#include <string>
#include <vector>

int assemble_file(const char* filename, const char* output_file, int debug) {
    FILE* in = nullptr;
    {
        typedef std::unique_ptr<FILE, int (*)(FILE*)> file_ptr;
        std::vector<std::string> lines;
        std::string preprocessed;
        if (!blackbox::tools::preprocess_includes(filename, preprocessed)) {
            return 1;
        }

        if (bbxc::asm_helpers::collect_lines_from_buffer(preprocessed.c_str(), lines) != 0) {
            return 1;
        }

        file_ptr tmp(tmpfile(), fclose);
        if (!tmp) {
            perror("tmpfile");
            return 1;
        }

        std::vector<Macro> macros;

        for (size_t i = 0; i < lines.size(); i++) {
            std::string trimmed = bbxc::asm_helpers::trim_copy(lines[i]);
            if (blackbox::tools::starts_with_ci(trimmed.c_str(), "%macro") && trimmed.size() > 6 &&
                (trimmed[6] == ' ' || trimmed[6] == '\t')) {
                std::string header = bbxc::asm_helpers::trim_copy(trimmed.substr(6));
                std::vector<std::string> header_tokens = bbxc::asm_helpers::split_tokens(header);
                if (header_tokens.empty()) {
                    std::println(stderr, "Syntax error: bad %macro header");
                    continue;
                }

                std::vector<std::string> params;
                params.reserve(header_tokens.size() > 1 ? header_tokens.size() - 1 : 0);
                for (size_t p = 1; p < header_tokens.size(); p++) {
                    params.push_back(header_tokens[p]);
                }

                std::vector<std::string> body;
                size_t j = i + 1;
                for (; j < lines.size(); j++) {
                    std::string line_trimmed = bbxc::asm_helpers::trim_copy(lines[j]);
                    if (blackbox::tools::equals_ci(line_trimmed.c_str(), "%endmacro")) {
                        break;
                    }
                    body.push_back(lines[j]);
                }

                Macro macro = {};
                if (!bbxc::asm_helpers::build_macro_owned(header_tokens[0], params, body, macro)) {
                    std::println(stderr, "Out of memory");
                    return 1;
                }
                macros.push_back(macro);

                i = j;
            }
        }

        unsigned long expand_id = 0;

        for (size_t i = 0; i < lines.size(); i++) {
            std::string trimmed = bbxc::asm_helpers::trim_copy(lines[i]);
            if (blackbox::tools::starts_with_ci(trimmed.c_str(), "%macro")) {
                size_t j = i + 1;
                for (; j < lines.size(); j++) {
                    std::string line_trimmed = bbxc::asm_helpers::trim_copy(lines[j]);
                    if (blackbox::tools::equals_ci(line_trimmed.c_str(), "%endmacro")) {
                        break;
                    }
                }
                i = j;
                continue;
            }
            if (!trimmed.empty() && trimmed[0] == '%') {
                if (blackbox::tools::equals_ci(trimmed.c_str(), "%asm") ||
                    blackbox::tools::equals_ci(trimmed.c_str(), "%data") ||
                    blackbox::tools::equals_ci(trimmed.c_str(), "%main") ||
                    blackbox::tools::equals_ci(trimmed.c_str(), "%entry") ||
                    blackbox::tools::equals_ci(trimmed.c_str(), "%endmacro")) {
                    (void)fputs(lines[i].c_str(), tmp.get());
                    continue;
                }
                if (blackbox::tools::expand_invocation(trimmed.c_str(), tmp.get(), 0, macros.data(),
                                                       macros.size(), &expand_id)) {
                    continue;
                }
            }
            (void)fputs(lines[i].c_str(), tmp.get());
        }

        for (auto& macro : macros) {
            bbxc::asm_helpers::free_macro_owned(macro);
        }

        fseek(tmp.get(), 0, SEEK_SET);
        in = tmp.release();
    }
    FILE* out = fopen(output_file, "wb");
    if (!out) {
        perror("fopen output");
        fclose(in);
        return 1;
    }

    char line[8192];
    int lineno = 0;

    while (fgets(line, sizeof(line), in)) {
        lineno++;
        char* s = blackbox::tools::trim(line);

        if (char* comment = strchr(s, ';')) {
            *comment = '\0';

            s = blackbox::tools::trim(s);
        }
        if (*s == '\0') {
            continue;
        }

        if (blackbox::tools::equals_ci(s, "%asm")) {
            break;
        } else {
            return bbxc::asm_helpers::failf(in, out, "Error: file must start with %%asm (line %d)",
                                            lineno);
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

    while (fgets(line, sizeof(line), in)) {
        lineno++;
        char* s = blackbox::tools::trim(line);
        if (char* comment = strchr(s, ';')) {
            *comment = '\0';
            s = blackbox::tools::trim(s);
        }
        if (*s == '\0' || *s == ';') {
            continue;
        }

        if (blackbox::tools::equals_ci(s, "%data")) {
            if (found_code_section) {
                return bbxc::asm_helpers::failf(
                    in, out, "Error on line %d: %%data section must come before %%main/%%entry",
                    lineno);
            }
            current_section = 1;
            if (debug) {
                std::print("[DEBUG] Entering data section at line {}\n", lineno);
            }
            continue;
        }
        if (blackbox::tools::equals_ci(s, "%main") == 1 ||
            blackbox::tools::equals_ci(s, "%entry") == 1) {
            current_section = 2;
            found_code_section = 1;
            if (debug) {
                std::print("[DEBUG] Entering code section at line {}\n", lineno);
            }
            continue;
        }

        if (blackbox::tools::starts_with_ci(s, "STR ")) {
            if (current_section != 1) {
                return bbxc::asm_helpers::failf(
                    in, out, "Error on line %d: STR must be inside %%data section", lineno);
            }
            char name[32];
            char* quote_start = strchr(s, '"');
            if (!quote_start || sscanf(s + 4, " $%31[^,]", name) != 1) {
                return bbxc::asm_helpers::failf(
                    in, out, "Syntax error line %d: expected STR $name, \"value\"", lineno);
            }

            quote_start++;
            char* quote_end = strchr(quote_start, '"');
            if (!quote_end) {
                return bbxc::asm_helpers::failf(
                    in, out, "Syntax error line %d: missing closing quote", lineno);
            }
            size_t len = quote_end - quote_start;

            if (data.size() >= 256) {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            data_table.insert(data_table.end(), quote_start, quote_start + len);
            data_table.push_back('\0');
            bbxc::asm_helpers::append_data_item(data, name, DATA_STRING, data_table_size, 0);
            data_table_size += static_cast<uint32_t>(len + 1);
            if (debug) {
                std::println("[DEBUG] String ${} at offset {}", name, data.back().offset);
            }
            continue;
        } else if (blackbox::tools::starts_with_ci(s, "DWORD ")) {
            if (current_section != 1) {
                return bbxc::asm_helpers::failf(
                    in, out, "Error on line %d: DWORD must be inside %%data section", lineno);
            }
            char name[32];
            uint32_t value;
            if (sscanf(s + 6, " $%31[^,], %u", name, &value) != 2) {
                return bbxc::asm_helpers::failf(
                    in, out, "Syntax error line %d: expected DWORD $name, value", lineno);
            }

            if (data.size() >= 256) {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            bbxc::asm_helpers::append_le_bytes(data_table, value, sizeof(value));
            bbxc::asm_helpers::append_data_item(data, name, DATA_DWORD, data_table_size, value);
            data_table_size += static_cast<uint32_t>(sizeof(value));
            if (debug) {
                std::println("[DEBUG] DWORD ${} at offset {}", name, data.back().offset);
            }
            continue;
        } else if (blackbox::tools::starts_with_ci(s, "QWORD ")) {
            if (current_section != 1) {
                return bbxc::asm_helpers::failf(
                    in, out, "Error on line %d: QWORD must be inside %%data section", lineno);
            }
            char name[32];
            char value_str[64];
            uint64_t value;
            if (sscanf(s + 6, " $%31[^,], %63s", name, value_str) != 2) {
                return bbxc::asm_helpers::failf(
                    in, out, "Syntax error line %d: expected QWORD $name, value", lineno);
            }
            value = (uint64_t) strtoull(value_str, nullptr, 0);

            if (data.size() >= 256) {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            bbxc::asm_helpers::append_le_bytes(data_table, value, sizeof(value));
            bbxc::asm_helpers::append_data_item(data, name, DATA_QWORD, data_table_size, value);
            data_table_size += static_cast<uint32_t>(sizeof(value));
            if (debug) {
                std::println("[DEBUG] QWORD ${} at offset {}", name, data.back().offset);
            }
            continue;
        } else if (blackbox::tools::starts_with_ci(s, "WORD ")) {
            if (current_section != 1) {
                return bbxc::asm_helpers::failf(
                    in, out, "Error on line %d: WORD must be inside %%data section", lineno);
            }
            char name[32];
            uint16_t value;
            if (sscanf(s + 5, " $%31[^,], %hu", name, &value) != 2) {
                return bbxc::asm_helpers::failf(
                    in, out, "Syntax error line %d: expected WORD $name, value", lineno);
            }

            if (data.size() >= 256) {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            bbxc::asm_helpers::append_le_bytes(data_table, value, sizeof(value));
            bbxc::asm_helpers::append_data_item(data, name, DATA_WORD, data_table_size, value);
            data_table_size += static_cast<uint32_t>(sizeof(value));
            if (debug) {
                std::println("[DEBUG] WORD ${} at offset {}", name, data.back().offset);
            }
            continue;
        } else if (blackbox::tools::starts_with_ci(s, "BYTE ")) {
            if (current_section != 1) {
                return bbxc::asm_helpers::failf(
                    in, out, "Error on line %d: BYTE must be inside %%data section", lineno);
            }
            char name[32];
            uint8_t value;
            if (sscanf(s + 5, " $%31[^,], %hhu", name, &value) != 2) {
                return bbxc::asm_helpers::failf(
                    in, out, "Syntax error line %d: expected BYTE $name, value", lineno);
            }

            if (data.size() >= 256) {
                return bbxc::asm_helpers::failf(in, out, "Too many data entries (max 256)");
            }

            data_table.push_back(value);
            bbxc::asm_helpers::append_data_item(data, name, DATA_BYTE, data_table_size, value);
            data_table_size += static_cast<uint32_t>(sizeof(value));
            if (debug) {
                std::println("[DEBUG] BYTE ${} at offset {}", name, data.back().offset);
            }
            continue;
        }

        if (current_section != 2) {
            if (size_t len = strlen(s); s[0] == '.' && s[len - 1] == ':') {
                return bbxc::asm_helpers::failf(
                    in, out, "Error on line %d: labels must be inside %%main/%%entry section",
                    lineno);
            }
            if (current_section == 0) {
                return bbxc::asm_helpers::failf(
                    in, out,
                    "Error on line %d: code outside of section. Use %%string or %%main/%%entry",
                    lineno);
            }
            continue;
        }

        if (size_t len = strlen(s); s[0] == '.' && s[len - 1] == ':') {
            if (labels.size() >= 256) {
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
                                   static_cast<unsigned>(labels.back().addr));
            continue;
        }

        if (blackbox::tools::starts_with_ci(s, "frame")) {
            if (labels.empty()) {
                return bbxc::asm_helpers::failf(
                    in, out, "Error on line %d: FRAME must follow a label", lineno);
            }
            char framebuf[32];
            if (sscanf(s + 5, " %31s", framebuf) != 1) {
                return bbxc::asm_helpers::failf(
                    in, out, "Syntax error on line %d: expected FRAME <slots>", lineno);
            }
            uint32_t fs = static_cast<uint32_t>(strtoul(framebuf, nullptr, 0));
            labels.back().frame_size = fs;
            continue;
        }

        pc += static_cast<uint32_t>(blackbox::tools::instr_size(s));
    }

    if (!found_code_section) {
        std::println(stderr, "Error: missing %main or %entry section");
        fclose(in);
        fclose(out);
        return 1;
    }

    uint32_t header_offset = (HEADER_FIXED_SIZE - MAGIC_SIZE) + data_table_size;
    for (auto& label : labels) {
        label.addr += header_offset;
    }

    rewind(in);
    lineno = 0;
    current_section = 0;

    fputc((MAGIC >> 16) & 0xFF, out);
    fputc((MAGIC >> 8) & 0xFF, out);
    fputc((MAGIC >> 0) & 0xFF, out);
    fputc(static_cast<uint8_t>(data.size()), out);
    blackbox::data::write_u32(out, data_table_size);
    if (data_table_size > 0) {
        fwrite(data_table.data(), 1, data_table_size, out);
    }

    while (fgets(line, sizeof(line), in)) {
        lineno++;
        char* s = blackbox::tools::trim(line);
        if (char* comment = strchr(s, ';')) {
            *comment = '\0';
            s = blackbox::tools::trim(s);
        }
        if (*s == '\0' || *s == ';') {
            continue;
        }
        if (blackbox::tools::equals_ci(s, "%data")) {
            current_section = 1;
            continue;
        }
        if (blackbox::tools::equals_ci(s, "%main") == 1 ||
            blackbox::tools::equals_ci(s, "%entry") == 1) {
            current_section = 2;
            continue;
        }

        if (current_section == 1) {
            continue;
        }

        if (current_section != 2) {
            continue;
        }

        if (blackbox::tools::starts_with_ci(s, "STR ")) {
            std::println(stderr,
                         "Syntax error on line {}: STR directive not allowed in code section",
                         lineno);
            fclose(in);
            fclose(out);
            return 1;
        }
        if (blackbox::tools::starts_with_ci(s, "DWORD ")) {
            std::println(stderr,
                         "Syntax error on line {}: DWORD directive not allowed in code section",
                         lineno);
            fclose(in);
            fclose(out);
            return 1;
        }

        if (blackbox::tools::starts_with_ci(s, "QWORD ")) {
            std::println(stderr,
                         "Syntax error on line {}: QWORD directive not allowed in code section",
                         lineno);
            fclose(in);
            fclose(out);
            return 1;
        }
        if (blackbox::tools::starts_with_ci(s, "WORD ")) {
            std::println(stderr,
                         "Syntax error on line {}: WORD directive not allowed in code section",
                         lineno);
            fclose(in);
            fclose(out);
            return 1;
        }
        if (blackbox::tools::starts_with_ci(s, "BYTE ")) {
            std::println(stderr,
                         "Syntax error on line {}: BYTE directive not allowed in code section",
                         lineno);
            fclose(in);
            fclose(out);
            return 1;
        } else if (blackbox::tools::starts_with_ci(s, "write")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char fd_str[16];
            char* str_start;

            if (sscanf(s + 5, " %15[^, \t]", fd_str) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected WRITE <STDOUT|STDERR> \"<string>\"",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t fd;
            if (blackbox::tools::equals_ci(fd_str, "STDOUT")) {
                fd = 1;
            } else if (blackbox::tools::equals_ci(fd_str, "STDERR")) {
                fd = 2;
            } else {
                std::println(stderr,
                             "Invalid file descriptor on line {}: {} (expected STDOUT or stderr)",
                             lineno, fd_str);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start = strchr(s, '"');
            if (!str_start) {
                std::println(stderr,
                             "Syntax error on line {}: missing opening quote for string",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start++;

            char* str_end = strchr(str_start, '"');
            if (!str_end) {
                std::println(stderr,
                             "Syntax error on line {}: missing closing quote for string",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            size_t len = str_end - str_start;
            len = std::min<size_t>(len, 255);

            fputc(opcode_to_byte(Opcode::WRITE), out);
            fputc(fd, out);
            fputc(static_cast<uint8_t>(len), out);

            for (size_t i = 0; i < len; i++) {
                fputc(static_cast<uint8_t>(str_start[i]), out);
            }
        } else if (blackbox::tools::starts_with_ci(s, "loadstr")) {
            char name[32];
            char regname[16];
            if (sscanf(s + 7, " $%31[^,], %15s", name, regname) != 2) {
                std::println(stderr,
                             "Syntax error line {}: expected LOADSTR $name, <register>",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = blackbox::tools::find_data(name, data.data(), data.size());
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::LOADSTR), out);
            fputc(reg, out);
            uint32_t mem_offset = data[offset].offset;
            blackbox::data::write_u32(out, mem_offset);
            if (debug) {
                std::println("[DEBUG] LOADSTR ${} (offset={}) -> {}", name, offset, regname);
            }
        } else if (blackbox::tools::starts_with_ci(s, "printstr")) {
            if (debug) {
                std::println("[DEBUG] Encoding instruction: {}", s);
            }
            char regname[16];
            if (sscanf(s + 8, " %15s", regname) != 1) {
                std::println(stderr, "Syntax error line {}: expected PRINTSTR <register>", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::PRINTSTR), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "eprintstr")) {
            if (debug) {
                std::println("[DEBUG] Encoding instruction: {}", s);
            }
            char regname[16];
            if (sscanf(s + 9, " %15s", regname) != 1) {
                std::println(stderr, "Syntax error line {}: expected EPRINTSTR <register>", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::EPRINTSTR), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "loadbyte")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char name[32];
            char regname[16];
            if (sscanf(s + 8, " $%31[^,], %15s", name, regname) != 2) {
                std::println(stderr,
                             "Syntax error line {}: expected LOADBYTE $name, <register>",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = blackbox::tools::find_data(name, data.data(), data.size());
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::LOADBYTE), out);
            fputc(reg, out);
            Data* d = &data[offset];
            if (d->type != DATA_BYTE) {
                std::println(stderr, "Warning line {}: LOADBYTE used on ${} which is not a BYTE",
                             lineno, name);
            }
            uint32_t offset_in_table = d->offset;
            blackbox::data::write_u32(out, offset_in_table);
            if (debug) {
                std::println("[DEBUG] LOADBYTE ${} (offset={}) -> {}", name, offset, regname);
            }
        } else if (blackbox::tools::starts_with_ci(s, "loadword")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char name[32];
            char regname[16];
            if (sscanf(s + 8, " $%31[^,], %15s", name, regname) != 2) {
                std::println(stderr,
                             "Syntax error line {}: expected LOADWORD $name, <register>",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = blackbox::tools::find_data(name, data.data(), data.size());
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::LOADWORD), out);
            fputc(reg, out);
            Data* d = &data[offset];
            if (d->type != DATA_WORD) {
                std::println(stderr, "Warning line {}: LOADWORD used on ${} which is not a WORD",
                             lineno, name);
            }
            uint32_t offset_in_table = d->offset;
            blackbox::data::write_u32(out, offset_in_table);
            if (debug) {
                std::println("[DEBUG] LOADWORD ${} (offset={}) -> {}", name, offset, regname);
            }
        } else if (blackbox::tools::starts_with_ci(s, "loaddword")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char name[32];
            char regname[16];
            if (sscanf(s + 9, " $%31[^,], %15s", name, regname) != 2) {
                std::println(stderr,
                             "Syntax error line {}: expected LOADDWORD $name, <register>",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = blackbox::tools::find_data(name, data.data(), data.size());
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::LOADDWORD), out);
            fputc(reg, out);
            Data* d = &data[offset];
            if (d->type != DATA_DWORD) {
                std::println(stderr, "Warning line {}: LOADDWORD used on ${} which is not a DWORD",
                             lineno, name);
            }
            uint32_t offset_in_table = d->offset;
            blackbox::data::write_u32(out, offset_in_table);
            if (debug) {
                std::println("[DEBUG] LOADDWORD ${} (offset={}) -> {}", name, offset, regname);
            }
        } else if (blackbox::tools::starts_with_ci(s, "loadqword")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char name[32];
            char regname[16];
            if (sscanf(s + 9, " $%31[^,], %15s", name, regname) != 2) {
                std::println(stderr,
                             "Syntax error line {}: expected LOADQWORD $name, <register>",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t offset = blackbox::tools::find_data(name, data.data(), data.size());
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::LOADQWORD), out);
            fputc(reg, out);
            Data* d = &data[offset];
            if (d->type != DATA_QWORD) {
                std::println(stderr, "Warning line {}: LOADQWORD used on ${} which is not a QWORD",
                             lineno, name);
            }
            uint32_t offset_in_table = d->offset;
            blackbox::data::write_u32(out, offset_in_table);
            if (debug) {
                std::println("[DEBUG] LOADQWORD ${} (offset={}) -> {}", name, offset, regname);
            }
        } else if (blackbox::tools::starts_with_ci(s, "continue")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(opcode_to_byte(Opcode::CONTINUE), out);
        } else if (blackbox::tools::equals_ci(s, "newline")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(opcode_to_byte(Opcode::NEWLINE), out);
        } else if (s[0] == '.') {
            continue;
        } else if (blackbox::tools::starts_with_ci(s, "je")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char label[32];
            if (sscanf(s + 2, " %31s", label) != 1) {
                std::println(stderr, "Syntax error on line {}: expected JE <label>\nGot: {}", lineno,
                             s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = blackbox::tools::find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JE to %s (addr=%u)\n", label, static_cast<unsigned>(addr));
            fputc(opcode_to_byte(Opcode::JE), out);
            blackbox::data::write_u32(out, addr);
        } else if (blackbox::tools::starts_with_ci(s, "jne")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char label[32];
            if (sscanf(s + 3, " %31s", label) != 1) {
                std::println(stderr, "Syntax error on line {}: expected JNE <label>\nGot: {}", lineno,
                             s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = blackbox::tools::find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JNE to %s (addr=%u)\n", label, static_cast<unsigned>(addr));
            fputc(opcode_to_byte(Opcode::JNE), out);
            blackbox::data::write_u32(out, addr);
        }

        else if (blackbox::tools::starts_with_ci(s, "halt")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char token[32];
            if (sscanf(s + 4, " %31s", token) == 1) {
                for (int t = 0; token[t]; t++) {
                    if (token[t] == '\r' || token[t] == '\n') {
                        bbxc::asm_helpers::trim_crlf(token);
                        break;
                    }
                }
                uint8_t val = 0;
                if (blackbox::tools::equals_ci(token, "ok")) {
                    val = 0;
                } else if (blackbox::tools::equals_ci(token, "bad")) {
                    val = 1;
                } else {
                    char* endp = nullptr;
                    unsigned long v = strtoul(token, &endp, 0);
                    if (endp == nullptr || *endp != '\0') {
                        std::println(stderr,
                                     "Syntax error on line {}: invalid HALT operand '{}'\nGot: {}",
                                     lineno, token, s);
                        fclose(in);
                        fclose(out);
                        return 1;
                    }
                    val = static_cast<uint8_t>(v);
                }
                fputc(opcode_to_byte(Opcode::HALT), out);
                fputc(val, out);
            } else {
                fputc(opcode_to_byte(Opcode::HALT), out);
            }
        } else if (blackbox::tools::starts_with_ci(s, "inc")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];

            if (sscanf(s + 3, " %7s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected INC <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::INC), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "dec")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];

            if (sscanf(s + 3, " %7s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected DEC <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::DEC), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "printreg")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];

            if (sscanf(s + 8, " %7s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected PRINTREG <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);

            fputc(opcode_to_byte(Opcode::PRINTREG), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "eprintreg")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];

            if (sscanf(s + 9, " %7s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected EPRINTREG <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);

            fputc(opcode_to_byte(Opcode::EPRINTREG), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "print_stacksize")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(opcode_to_byte(Opcode::PRINT_STACKSIZE), out);
        } else if (blackbox::tools::starts_with_ci(s, "PRINTCHAR")) {
            char reg[16];
            if (sscanf(s + 9, " %15s", reg) != 1) {
                std::println(stderr, "Syntax error line {}: expected PRINTCHAR <register>", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::PRINTCHAR), out);
            fputc(blackbox::tools::parse_register(reg, lineno), out);
        } else if (blackbox::tools::starts_with_ci(s, "EPRINTCHAR")) {
            char reg[16];
            if (sscanf(s + 10, " %15s", reg) != 1) {
                std::println(stderr, "Syntax error line {}: expected EPRINTCHAR <register>", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::EPRINTCHAR), out);
            fputc(blackbox::tools::parse_register(reg, lineno), out);
        } else if (blackbox::tools::starts_with_ci(s, "print")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char c;
            if (sscanf(s + 5, " '%c", &c) != 1) {
                std::println(stderr, "Syntax error on line {}\nGot: {}", lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::PRINT), out);
            fputc(static_cast<uint8_t>(c), out);
        } else if (blackbox::tools::starts_with_ci(s, "jmpi")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            uint32_t addr;
            if (sscanf(s + 5, " %u", &addr) != 1) {
                std::println(stderr, "Syntax error on line {}: expected JMPI <addr>\nGot: {}", lineno,
                             line);
                fclose(in);
                fclose(out);
                return 1;
            }
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JMPI to addr %u\n", addr);
            fputc(opcode_to_byte(Opcode::JMPI), out);
            blackbox::data::write_u32(out, addr);
        } else if (blackbox::tools::starts_with_ci(s, "jmp")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char label_name[32];
            if (sscanf(s + 3, " %31s", label_name) == 0) {
                std::println(stderr, "Syntax error on line {}: expected JMP <label>\nGot: {}", lineno,
                             line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = blackbox::tools::find_label(label_name, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JMP to %s (addr=%u)\n", label_name,
                                   static_cast<unsigned>(addr));
            fputc(opcode_to_byte(Opcode::JMP), out);
            blackbox::data::write_u32(out, addr);
        } else if (blackbox::tools::starts_with_ci(s, "pop")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            if (sscanf(s + 3, " %3s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected POP <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::POP), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "add")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s , %3s", dst_reg, src_reg) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected ADD <src>, <dst>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = blackbox::tools::parse_register(dst_reg, lineno);
            uint8_t src = blackbox::tools::parse_register(src_reg, lineno);
            fputc(opcode_to_byte(Opcode::ADD), out);
            fputc(dst, out);
            fputc(src, out);
        } else if (blackbox::tools::starts_with_ci(s, "sub")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s , %3s", dst_reg, src_reg) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected SUB <dst>, <src>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = blackbox::tools::parse_register(dst_reg, lineno);
            uint8_t src = blackbox::tools::parse_register(src_reg, lineno);
            fputc(opcode_to_byte(Opcode::SUB), out);
            fputc(dst, out);
            fputc(src, out);
        } else if (blackbox::tools::starts_with_ci(s, "mul")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s, %3s", dst_reg, src_reg) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected MUL <dst>, <src>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = blackbox::tools::parse_register(dst_reg, lineno);
            uint8_t src = blackbox::tools::parse_register(src_reg, lineno);
            fputc(opcode_to_byte(Opcode::MUL), out);
            fputc(dst, out);
            fputc(src, out);
        } else if (blackbox::tools::starts_with_ci(s, "div")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s, %3s", dst_reg, src_reg) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected DIV <dst>, <src>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = blackbox::tools::parse_register(dst_reg, lineno);
            uint8_t src = blackbox::tools::parse_register(src_reg, lineno);
            fputc(opcode_to_byte(Opcode::DIV), out);
            fputc(dst, out);
            fputc(src, out);
        } else if (blackbox::tools::starts_with_ci(s, "movi")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char dst_reg[16];
            char src[16];

            if (sscanf(s + 4, " %3s, %15s", dst_reg, src) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected MOVI <dst>, <value>\nGot: {}\n(If you're using a 2 character register like R1 or R2, use R01 or R02 instead!)",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = blackbox::tools::parse_register(dst_reg, lineno);
            if (src[0] == '\'') {
                int32_t imm = (int32_t) static_cast<unsigned char>(src[1]);
                fputc(opcode_to_byte(Opcode::MOVI), out);
                fputc(dst, out);
                blackbox::data::write_u32(out, imm);
            } else {
                int32_t imm = strtol(src, nullptr, 0);
                fputc(opcode_to_byte(Opcode::MOVI), out);
                fputc(dst, out);
                blackbox::data::write_u32(out, imm);
            }
        } else if (blackbox::tools::starts_with_ci(s, "mov")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char dst_reg[16];
            char src_reg[16];

            if (sscanf(s + 3, " %15[^,], %15s", dst_reg, src_reg) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected MOV <dst>, <src>\nGot: {}\n(If you're using a 2 character register like R1 or R2, use R01 or R02 instead!)",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = blackbox::tools::parse_register(dst_reg, lineno);
            uint8_t src = blackbox::tools::parse_register(src_reg, lineno);
            fputc(opcode_to_byte(Opcode::MOV_REG), out);
            fputc(dst, out);
            fputc(src, out);
        } else if (blackbox::tools::starts_with_ci(s, "push ")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            if (sscanf(s + 5, " %3s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected PUSH <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::PUSH_REG), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "pushi ")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char operand[16];
            if (sscanf(s + 6, " %15s", operand) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected pushi <value|register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            bbxc::asm_helpers::dbg(debug, "[DEBUG] pushi %s\n", operand);
            char* end;
            int32_t imm = strtol(operand, &end, 0);
            if (*end != '\0') {
                std::println(stderr, "Invalid immediate on line {}: {}", lineno, operand);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::PUSHI), out);
            blackbox::data::write_u32(out, imm);
        }

        else if (blackbox::tools::starts_with_ci(s, "cmp")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %3s, %3s", reg1, reg2) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected CMP <reg1>, <reg2>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t r1 = blackbox::tools::parse_register(reg1, lineno);
            uint8_t r2 = blackbox::tools::parse_register(reg2, lineno);
            fputc(opcode_to_byte(Opcode::CMP), out);
            fputc(r1, out);
            fputc(r2, out);
        } else if (blackbox::tools::starts_with_ci(s, "alloc")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char operand[32];
            if (sscanf(s + 5, " %31s", operand) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected ALLOC <elements>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, nullptr, 0);
            fputc(opcode_to_byte(Opcode::ALLOC), out);
            blackbox::data::write_u32(out, num);
        } else if (blackbox::tools::starts_with_ci(s, "frame")) {
            continue;
        } else if (blackbox::tools::starts_with_ci(s, "loadvar")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            char addrname[32];
            if (sscanf(s + 7, " %3s, %31s", regname, addrname) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected LOADVAR <register>, <slot|Rxx>\nGot: {}",
                             lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            if (toupper(static_cast<unsigned char>(addrname[0])) == 'R') {
                uint8_t idx = blackbox::tools::parse_register(addrname, lineno);
                fputc(opcode_to_byte(Opcode::LOADVAR_REG), out);
                fputc(reg, out);
                fputc(idx, out);
            } else {
                uint32_t slot = strtoul(addrname, nullptr, 0);
                fputc(opcode_to_byte(Opcode::LOADVAR), out);
                fputc(reg, out);
                blackbox::data::write_u32(out, slot);
            }
        } else if (blackbox::tools::starts_with_ci(s, "LOADREF")) {
            char rdst[16], rsrc[16];
            if (sscanf(s + 7, " %15[^,], %15s", rdst, rsrc) != 2) {
                std::println(stderr, "Syntax error on line {}: expected LOADREF <dst>, <src>\nGot: {}",
                             lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = blackbox::tools::parse_register(rdst, lineno);
            uint8_t src = blackbox::tools::parse_register(rsrc, lineno);
            fputc(opcode_to_byte(Opcode::LOADREF), out);
            fputc(dst, out);
            fputc(src, out);
        } else if (blackbox::tools::starts_with_ci(s, "STOREREF")) {
            char rdst[16], rsrc[16];
            if (sscanf(s + 8, " %15[^,], %15s", rdst, rsrc) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected STOREREF <dst>, <src>\nGot: {}",
                             lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = blackbox::tools::parse_register(rdst, lineno);
            uint8_t src = blackbox::tools::parse_register(rsrc, lineno);
            fputc(opcode_to_byte(Opcode::STOREREF), out);
            fputc(dst, out);
            fputc(src, out);
        } else if (blackbox::tools::starts_with_ci(s, "load")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            char addrname[32];
            if (sscanf(s + 4, " %3s, %31s", regname, addrname) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected LOAD <register>, <index in stack|Rxx>\nGot: {}",
                             lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            if (toupper(static_cast<unsigned char>(addrname[0])) == 'R') {
                uint8_t idx = blackbox::tools::parse_register(addrname, lineno);
                fputc(opcode_to_byte(Opcode::LOAD_REG), out);
                fputc(reg, out);
                fputc(idx, out);
            } else {
                uint32_t addr = strtoul(addrname, nullptr, 0);
                fputc(opcode_to_byte(Opcode::LOAD), out);
                fputc(reg, out);
                blackbox::data::write_u32(out, addr);
            }
        } else if (blackbox::tools::starts_with_ci(s, "storevar")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            char addrname[32];
            if (sscanf(s + 8, " %3s, %31s", regname, addrname) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected STOREVAR <register>, <slot|Rxx>\nGot: {}",
                             lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            if (toupper(static_cast<unsigned char>(addrname[0])) == 'R') {
                uint8_t idx = blackbox::tools::parse_register(addrname, lineno);
                fputc(opcode_to_byte(Opcode::STOREVAR_REG), out);
                fputc(reg, out);
                fputc(idx, out);
            } else {
                uint32_t slot = strtoul(addrname, nullptr, 0);
                fputc(opcode_to_byte(Opcode::STOREVAR), out);
                fputc(reg, out);
                blackbox::data::write_u32(out, slot);
            }
        } else if (blackbox::tools::starts_with_ci(s, "store")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            char addrname[32];
            if (sscanf(s + 5, " %3s, %31s", regname, addrname) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected STORE <register>, <index in stack|Rxx>\nGot: {}",
                             lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            if (toupper(static_cast<unsigned char>(addrname[0])) == 'R') {
                uint8_t idx = blackbox::tools::parse_register(addrname, lineno);
                fputc(opcode_to_byte(Opcode::STORE_REG), out);
                fputc(reg, out);
                fputc(idx, out);
            } else {
                uint32_t addr = strtoul(addrname, nullptr, 0);
                fputc(opcode_to_byte(Opcode::STORE), out);
                fputc(reg, out);
                blackbox::data::write_u32(out, addr);
            }
        } else if (blackbox::tools::starts_with_ci(s, "grow")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char operand[32];
            if (sscanf(s + 4, " %31s", operand) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected GROW <additional elements>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, nullptr, 0);
            fputc(opcode_to_byte(Opcode::GROW), out);
            blackbox::data::write_u32(out, num);
        } else if (blackbox::tools::starts_with_ci(s, "resize")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char operand[32];
            if (sscanf(s + 6, " %31s", operand) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected RESIZE <new size>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, nullptr, 0);
            fputc(opcode_to_byte(Opcode::RESIZE), out);
            blackbox::data::write_u32(out, num);
        } else if (blackbox::tools::starts_with_ci(s, "free")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char operand[32];
            if (sscanf(s + 4, " %31s", operand) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected FREE <number of elements>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t num = strtoul(operand, nullptr, 0);
            fputc(opcode_to_byte(Opcode::FREE), out);
            blackbox::data::write_u32(out, num);
        } else if (blackbox::tools::starts_with_ci(s, "fopen")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char filename[128];
            char mode_raw[8];
            char fid_raw[8];
            if (sscanf(s + 5, " %7[^,], %7[^,], \"%127[^\"]\"", mode_raw, fid_raw, filename) != 3) {
                std::println(stderr,
                             "Syntax error on line {}: expected FOPEN <mode>, <file_descriptor>, \"<filename>\"\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char* mode = blackbox::tools::trim(mode_raw);
            char* fid = blackbox::tools::trim(fid_raw);
            uint8_t mode_flag;
            if (blackbox::tools::equals_ci(mode, "r") == 1) {
                mode_flag = 0;
            } else if (blackbox::tools::equals_ci(mode, "w") == 1) {
                mode_flag = 1;
            } else if (blackbox::tools::equals_ci(mode, "a") == 1) {
                mode_flag = 2;
            } else {
                std::println(stderr, "Invalid mode on line {}: {} (expected r, w, or a)", lineno,
                             mode);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::FOPEN), out);
            fputc(mode_flag, out);
            uint8_t fd = blackbox::tools::parse_file(fid, lineno);
            fputc(fd, out);
            uint8_t fname_len = static_cast<uint8_t>(strlen(filename));
            fputc(fname_len, out);
            for (uint8_t i = 0; i < fname_len; i++) {
                fputc(static_cast<uint8_t>(filename[i]), out);
            }
        } else if (blackbox::tools::starts_with_ci(s, "fclose")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char fid[4];
            if (sscanf(s + 6, " %3s", fid) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected FCLOSE <file_descriptor>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t fd = blackbox::tools::parse_file(fid, lineno);
            fputc(opcode_to_byte(Opcode::FCLOSE), out);
            fputc(fd, out);
        } else if (blackbox::tools::starts_with_ci(s, "fread")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char fid_raw[8];
            char reg_raw[8];
            if (sscanf(s + 5, " %7[^,], %7[^,]", fid_raw, reg_raw) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected FREAD <file_descriptor>, <register to read into>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char* fid = blackbox::tools::trim(fid_raw);
            char* reg_tok = blackbox::tools::trim(reg_raw);
            uint8_t fd = blackbox::tools::parse_file(fid, lineno);
            uint8_t reg = blackbox::tools::parse_register(reg_tok, lineno);
            fputc(opcode_to_byte(Opcode::FREAD), out);
            fputc(fd, out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "fseek")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char fid_raw[8];
            char offset_raw[64];
            if (sscanf(s + 5, " %7[^,], %63[^\n]", fid_raw, offset_raw) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected FSEEK <file_descriptor>, <offset_value|offset_register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char* fid = blackbox::tools::trim(fid_raw);
            char* offset_tok = blackbox::tools::trim(offset_raw);
            uint8_t fd = blackbox::tools::parse_file(fid, lineno);
            if (offset_tok[0] == 'R') {
                uint8_t offset_reg = blackbox::tools::parse_register(offset_tok, lineno);
                fputc(opcode_to_byte(Opcode::FSEEK_REG), out);
                fputc(fd, out);
                fputc(offset_reg, out);
            } else if (offset_tok[0] == '"') {
                char inner[64];
                if (sscanf(offset_tok, "\"%63[^\"]\"", inner) != 1) {
                    std::println(stderr,
                                 "Syntax error on line {}: malformed quoted offset\nGot: {}",
                                 lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                int32_t offset_imm = static_cast<int32_t>(strtol(inner, nullptr, 0));
                fputc(opcode_to_byte(Opcode::FSEEK_IMM), out);
                fputc(fd, out);
                blackbox::data::write_u32(out, offset_imm);
            } else {
                int32_t offset_imm = static_cast<int32_t>(strtol(offset_tok, nullptr, 0));
                fputc(opcode_to_byte(Opcode::FSEEK_IMM), out);
                fputc(fd, out);
                blackbox::data::write_u32(out, offset_imm);
            }
        } else if (blackbox::tools::starts_with_ci(s, "FWRITE")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char fid_raw[8];
            char size_raw[64];
            if (sscanf(s + 6, " %7[^,], %63[^\n]", fid_raw, size_raw) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected FWRITE <file_descriptor>, <size_value|size_register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char* fid = blackbox::tools::trim(fid_raw);
            char* size_tok = blackbox::tools::trim(size_raw);
            uint8_t fd = blackbox::tools::parse_file(fid, lineno);
            if (size_tok[0] == 'R') {
                uint8_t size_reg = blackbox::tools::parse_register(size_tok, lineno);
                fputc(opcode_to_byte(Opcode::FWRITE_REG), out);
                fputc(fd, out);
                fputc(size_reg, out);
            } else if (size_tok[0] == '"') {
                char inner[64];
                if (sscanf(size_tok, "\"%63[^\"]\"", inner) != 1) {
                    std::println(stderr,
                                 "Syntax error on line {}: malformed quoted size\nGot: {}",
                                 lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                uint32_t size_imm = strtoul(inner, nullptr, 0);
                fputc(opcode_to_byte(Opcode::FWRITE_IMM), out);
                fputc(fd, out);
                blackbox::data::write_u32(out, size_imm);
            } else {
                uint32_t size_imm = strtoul(size_tok, nullptr, 0);
                fputc(opcode_to_byte(Opcode::FWRITE_IMM), out);
                fputc(fd, out);
                blackbox::data::write_u32(out, size_imm);
            }
        } else if (blackbox::tools::starts_with_ci(s, "NOT")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            if (sscanf(s + 3, " %3s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected NOT <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);
            fputc(opcode_to_byte(Opcode::NOT), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "AND")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %3s, %3s", reg1, reg2) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected AND <reg1>, <reg2>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t r1 = blackbox::tools::parse_register(reg1, lineno);
            uint8_t r2 = blackbox::tools::parse_register(reg2, lineno);
            fputc(opcode_to_byte(Opcode::AND), out);
            fputc(r1, out);
            fputc(r2, out);
        } else if (blackbox::tools::starts_with_ci(s, "OR")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 2, " %3s, %3s", reg1, reg2) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected OR <reg1>, <reg2>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t r1 = blackbox::tools::parse_register(reg1, lineno);
            uint8_t r2 = blackbox::tools::parse_register(reg2, lineno);
            fputc(opcode_to_byte(Opcode::OR), out);
            fputc(r1, out);
            fputc(r2, out);
        } else if (blackbox::tools::starts_with_ci(s, "XOR")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %3s, %3s", reg1, reg2) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected XOR <reg1>, <reg2>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t r1 = blackbox::tools::parse_register(reg1, lineno);
            uint8_t r2 = blackbox::tools::parse_register(reg2, lineno);
            fputc(opcode_to_byte(Opcode::XOR), out);
            fputc(r1, out);
            fputc(r2, out);
        } else if (blackbox::tools::starts_with_ci(s, "READCHAR")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];

            if (sscanf(s + 8, " %7s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected READCHAR <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);

            fputc(opcode_to_byte(Opcode::READCHAR), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "READSTR")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];

            if (sscanf(s + 7, " %7s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected READSTR <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);

            fputc(opcode_to_byte(Opcode::READSTR), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "SLEEP")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char operand[64];
            if (sscanf(s + 5, " %63s", operand) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected SLEEP <milliseconds>|<register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }

            if (toupper(static_cast<unsigned char>(operand[0])) == 'R') {
                uint8_t reg = blackbox::tools::parse_register(operand, lineno);
                fputc(opcode_to_byte(Opcode::SLEEP_REG), out);
                fputc(reg, out);
            } else {
                uint32_t ms = static_cast<uint32_t>(strtoul(operand, nullptr, 0));
                fputc(opcode_to_byte(Opcode::SLEEP), out);
                blackbox::data::write_u32(out, ms);
            }
        } else if (blackbox::tools::starts_with_ci(s, "clrscr")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(opcode_to_byte(Opcode::CLRSCR), out);
        } else if (blackbox::tools::starts_with_ci(s, "rand")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];
            char rest[64] = {};
            // RAND <reg>
            // RAND <reg>, <max>
            // RAND <reg>, <min>, <max>
            int matched = sscanf(s + 4, " %3s , %63[^\n]", regname, rest);
            if (matched < 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected RAND <register>[, <max>] or RAND <register>[, <min>, <max>]\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);

            fputc(opcode_to_byte(Opcode::RAND), out);
            fputc(reg, out);

            if (matched == 2 && rest[0] != '\0') {
                char a[32] = {}, b[32] = {};
                int cnt = sscanf(rest, " %31[^,] , %31s", a, b);
                if (cnt == 2) {
                    int32_t min = static_cast<int32_t>(strtol(blackbox::tools::trim(a), nullptr, 0));
                    int32_t max = static_cast<int32_t>(strtol(blackbox::tools::trim(b), nullptr, 0));
                    blackbox::data::write_u64(out, static_cast<uint64_t>(min));
                    blackbox::data::write_u64(out, static_cast<uint64_t>(max));
                } else {
                    blackbox::data::write_u64(out, static_cast<uint64_t>(INT64_MIN));
                    blackbox::data::write_u64(out, (uint64_t) INT64_MAX);
                }
            } else {
                blackbox::data::write_u64(out, static_cast<uint64_t>(INT64_MIN));
                blackbox::data::write_u64(out, (uint64_t) INT64_MAX);
            }
        } else if (blackbox::tools::starts_with_ci(s, "getkey")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];

            if (sscanf(s + 6, " %7s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected GETKEY <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);

            fputc(opcode_to_byte(Opcode::GETKEY), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "read")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char regname[16];

            if (sscanf(s + 4, " %7s", regname) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected READ <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t reg = blackbox::tools::parse_register(regname, lineno);

            fputc(opcode_to_byte(Opcode::READ), out);
            fputc(reg, out);
        } else if (blackbox::tools::starts_with_ci(s, "JL")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char label[32];
            if (sscanf(s + 3, " %31s", label) != 1) {
                std::println(stderr, "Syntax error on line {}: expected JL <label>\nGot: {}", lineno,
                             line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = blackbox::tools::find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JL to %s (addr=%u)\n", label, static_cast<unsigned>(addr));
            fputc(opcode_to_byte(Opcode::JL), out);
            blackbox::data::write_u32(out, addr);
        } else if (blackbox::tools::starts_with_ci(s, "JGE")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char label[32];
            if (sscanf(s + 3, " %31s", label) != 1) {
                std::println(stderr, "Syntax error on line {}: expected JGE <label>\nGot: {}", lineno,
                             line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = blackbox::tools::find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JGE to %s (addr=%u)\n", label, static_cast<unsigned>(addr));
            fputc(opcode_to_byte(Opcode::JGE), out);
            blackbox::data::write_u32(out, addr);
        } else if (blackbox::tools::starts_with_ci(s, "JB")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char label[32];
            if (sscanf(s + 2, " %31s", label) != 1) {
                std::println(stderr, "Syntax error on line {}: expected JB <label>\nGot: {}", lineno,
                             line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = blackbox::tools::find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JB to %s (addr=%u)\n", label, static_cast<unsigned>(addr));
            fputc(opcode_to_byte(Opcode::JB), out);
            blackbox::data::write_u32(out, addr);
        } else if (blackbox::tools::starts_with_ci(s, "JAE")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char label[32];
            if (sscanf(s + 3, " %31s", label) != 1) {
                std::println(stderr, "Syntax error on line {}: expected JAE <label>\nGot: {}", lineno,
                             line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = blackbox::tools::find_label(label, labels.data(), labels.size());
            bbxc::asm_helpers::dbg(debug, "[DEBUG] JAE to %s (addr=%u)\n", label, static_cast<unsigned>(addr));
            fputc(opcode_to_byte(Opcode::JAE), out);
            blackbox::data::write_u32(out, addr);
        } else if (blackbox::tools::starts_with_ci(s, "CALL")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char label[32];
            if (sscanf(s + 4, " %31s", label) != 1) {
                std::println(stderr, "Syntax error on line {}: expected CALL <label>\nGot: {}", lineno,
                             line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char* comma = strchr(s + 4, ',');
            uint32_t frame_size = 0;
            if (comma) {
                frame_size = static_cast<uint32_t>(strtoul(comma + 1, nullptr, 0));
            }
            uint32_t addr = blackbox::tools::find_label(label, labels.data(), labels.size());
            if (!comma) {
                for (size_t i = 0; i < labels.size(); i++) {
                    if (strcmp(labels[i].name, label) == 0) {
                        frame_size = labels[i].frame_size;
                        break;
                    }
                }
            }
            bbxc::asm_helpers::dbg(debug, "[DEBUG] CALL to %s (addr=%u frame=%u)\n", label,
                                   static_cast<unsigned>(addr), static_cast<unsigned>(frame_size));
            fputc(opcode_to_byte(Opcode::CALL), out);
            blackbox::data::write_u32(out, addr);
            blackbox::data::write_u32(out, frame_size);
        } else if (blackbox::tools::starts_with_ci(s, "RET")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(opcode_to_byte(Opcode::RET), out);
        } else if (blackbox::tools::starts_with_ci(s, "MOD")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char src_reg[16];
            char dst_reg[16];
            if (sscanf(s + 3, " %3s, %3s", dst_reg, src_reg) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected MOD <dst>, <src>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint8_t dst = blackbox::tools::parse_register(dst_reg, lineno);
            uint8_t src = blackbox::tools::parse_register(src_reg, lineno);
            fputc(opcode_to_byte(Opcode::MOD), out);
            fputc(dst, out);
            fputc(src, out);
        } else if (blackbox::tools::starts_with_ci(s, "BREAK")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(opcode_to_byte(Opcode::BREAK), out);
        } else if (blackbox::tools::starts_with_ci(s, "EXEC")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            char* str_start = strchr(s, '"');
            if (!str_start) {
                std::println(stderr, "Syntax error on line {}: missing opening quote for EXEC",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start++;

            char* str_end = strchr(str_start, '"');
            if (!str_end) {
                std::println(stderr, "Syntax error on line {}: missing closing quote for EXEC",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }

            size_t len = static_cast<size_t>(str_end - str_start);
            if (len > 255) {
                len = 255;
            }

            char* after = str_end + 1;
            while (*after == ' ' || *after == '\t' || *after == ',') {
                after++;
            }
            char regname[16];
            if (sscanf(after, " %15s", regname) != 1) {
                std::println(stderr, "Syntax error on line {}: expected EXEC \"<cmd>\", <register>",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            for (int i = 0; regname[i]; i++) {
                if (regname[i] == '\r' || regname[i] == '\n') {
                    bbxc::asm_helpers::trim_crlf(regname);
                    break;
                }
            }
            uint8_t dest_reg = blackbox::tools::parse_register(regname, lineno);

            fputc(opcode_to_byte(Opcode::EXEC), out);
            fputc(dest_reg, out);
            fputc(static_cast<uint8_t>(len), out);
            for (size_t i = 0; i < len; i++) {
                fputc(static_cast<uint8_t>(str_start[i]), out);
            }
            bbxc::asm_helpers::dbg(debug, "[DEBUG] EXEC -> %.*s (dest=%s)\n", static_cast<int>(len), str_start,
                                   regname);
        } else if (blackbox::tools::starts_with_ci(s, "REGSYSCALL")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            uint8_t id;
            char label[32];
            if (sscanf(s + 10, " %hhu, %31s", &id, label) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected REGSYSCALL <id>, <label>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = blackbox::tools::find_label(label, labels.data(), labels.size());
            fputc(opcode_to_byte(Opcode::REGSYSCALL), out);
            fputc(id, out);
            blackbox::data::write_u32(out, addr);
        } else if (blackbox::tools::starts_with_ci(s, "SYSCALL")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            uint8_t id;
            if (sscanf(s + 7, " %hhu", &id) != 1) {
                std::println(stderr, "Syntax error on line {}: expected SYSCALL <id>\nGot: {}", lineno,
                             line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::SYSCALL), out);
            fputc(id, out);
        } else if (blackbox::tools::starts_with_ci(s, "SYSRET")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(opcode_to_byte(Opcode::SYSRET), out);
        } else if (blackbox::tools::starts_with_ci(s, "DROPPRIV")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(opcode_to_byte(Opcode::DROPPRIV), out);
        } else if (blackbox::tools::starts_with_ci(s, "GETMODE")) {
            char reg[16];
            if (sscanf(s + 7, " %15s", reg) != 1) {
                std::println(stderr, "Syntax error on line {}: expected GETMODE <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::GETMODE), out);
            fputc(blackbox::tools::parse_register(reg, lineno), out);
        } else if (blackbox::tools::starts_with_ci(s, "SETPERM")) {
            uint32_t start, count;
            char perm_str[16];
            if (sscanf(s + 7, " %u, %u, %15s", &start, &count, perm_str) != 3) {
                std::println(stderr,
                             "Syntax error on line {}: expected SETPERM <start>, <count>, <priv>/<prot>\nGot: {}",
                             lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }

            char* slash = strchr(perm_str, '/');
            if (!slash) {
                std::println(stderr,
                             "Syntax error on line {}: SETPERM permissions must be <priv>/<prot> e.g. rw/r\nGot: {}",
                             lineno, s);
                fclose(in);
                fclose(out);
                return 1;
            }

            *slash = '\0';
            char* priv_str = perm_str;
            char* prot_str = slash + 1;

            uint8_t priv_read = strchr(priv_str, 'R') ? 1 : 0;
            uint8_t priv_write = strchr(priv_str, 'W') ? 1 : 0;
            uint8_t prot_read = strchr(prot_str, 'R') ? 1 : 0;
            uint8_t prot_write = strchr(prot_str, 'W') ? 1 : 0;

            fputc(opcode_to_byte(Opcode::SETPERM), out);
            blackbox::data::write_u32(out, start);
            blackbox::data::write_u32(out, count);
            fputc(priv_read, out);
            fputc(priv_write, out);
            fputc(prot_read, out);
            fputc(prot_write, out);
        } else if (blackbox::tools::starts_with_ci(s, "DUMPREGS")) {
            bbxc::asm_helpers::dbg(debug, "[DEBUG] Encoding instruction: %s\n", s);
            fputc(opcode_to_byte(Opcode::DUMPREGS), out);
        } else if (blackbox::tools::starts_with_ci(s, "REGFAULT")) {
            uint8_t id;
            char label[32];
            if (sscanf(s + 8, " %hhu, %31s", &id, label) != 2) {
                std::println(stderr,
                             "Syntax error on line {}: expected REGFAULT <id>, <label>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            uint32_t addr = blackbox::tools::find_label(label, labels.data(), labels.size());
            fputc(opcode_to_byte(Opcode::REGFAULT), out);
            fputc(id, out);
            blackbox::data::write_u32(out, addr);
        } else if (blackbox::tools::starts_with_ci(s, "FAULTRET")) {
            fputc(opcode_to_byte(Opcode::FAULTRET), out);
        } else if (blackbox::tools::starts_with_ci(s, "GETFAULT")) {
            char reg[16];
            if (sscanf(s + 8, " %15s", reg) != 1) {
                std::println(stderr, "Syntax error line {}: expected GETFAULT <register>", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::GETFAULT), out);
            fputc(blackbox::tools::parse_register(reg, lineno), out);
        } else if (blackbox::tools::starts_with_ci(s, "SHLI")) {
            char reg[16];
            char amount[32];
            char* comma = strchr(s + 4, ',');
            if (!comma || sscanf(s + 4, " %15[^,]", reg) != 1 ||
                sscanf(comma + 1, " %31s", amount) != 1) {
                std::println(stderr, "Syntax error line {}: expected SHLI <register>, <shift>",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (amount[0] == '-') {
                std::println(stderr, "Syntax error line {}: shift count must be non-negative", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char* end;
            uint64_t shift = strtoull(amount, &end, 0);
            if (*end != '\0') {
                std::println(stderr, "Syntax error line {}: invalid shift count '{}'", lineno, amount);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::SHLI), out);
            fputc(blackbox::tools::parse_register(reg, lineno), out);
            blackbox::data::write_u64(out, (uint64_t) shift);
        } else if (blackbox::tools::starts_with_ci(s, "SHRI")) {
            char reg[16];
            char amount[32];
            char* comma = strchr(s + 4, ',');
            if (!comma || sscanf(s + 4, " %15[^,]", reg) != 1 ||
                sscanf(comma + 1, " %31s", amount) != 1) {
                std::println(stderr, "Syntax error line {}: expected SHRI <register>, <shift>",
                             lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (amount[0] == '-') {
                std::println(stderr, "Syntax error line {}: shift count must be non-negative", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            char* end;
            uint64_t shift = strtoull(amount, &end, 0);
            if (*end != '\0') {
                std::println(stderr, "Syntax error line {}: invalid shift count '{}'", lineno, amount);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::SHRI), out);
            fputc(blackbox::tools::parse_register(reg, lineno), out);
            blackbox::data::write_u64(out, (uint64_t) shift);
        } else if (blackbox::tools::starts_with_ci(s, "SHL")) {
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %15[^,], %15s", reg1, reg2) != 2) {
                std::println(stderr, "Syntax error line {}: expected SHL <dst>, <src>", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::SHL), out);
            fputc(blackbox::tools::parse_register(reg1, lineno), out);
            fputc(blackbox::tools::parse_register(reg2, lineno), out);
        } else if (blackbox::tools::starts_with_ci(s, "SHR")) {
            char reg1[16];
            char reg2[16];
            if (sscanf(s + 3, " %15[^,], %15s", reg1, reg2) != 2) {
                std::println(stderr, "Syntax error line {}: expected SHR <dst>, <src>", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::SHR), out);
            fputc(blackbox::tools::parse_register(reg1, lineno), out);
            fputc(blackbox::tools::parse_register(reg2, lineno), out);
        } else if (blackbox::tools::starts_with_ci(s, "GETARGC")) {
            char reg[16];
            if (sscanf(s + 7, " %15s", reg) != 1) {
                std::println(stderr, "Syntax error on line {}: expected GETARGC <register>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::GETARGC), out);
            fputc(blackbox::tools::parse_register(reg, lineno), out);
        } else if (blackbox::tools::starts_with_ci(s, "GETARG")) {
            char reg[16];
            char index[32];
            char* comma = strchr(s + 7, ',');
            if (!comma || sscanf(s + 7, " %15[^,]", reg) != 1 ||
                sscanf(comma + 1, " %31s", index) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected GETARG <register>, <index>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            char* end;
            uint32_t idx = strtoul(index, &end, 0);
            if (*end != '\0') {
                std::println(stderr, "Syntax error on line {}: invalid argument index '{}'\nGot: {}",
                             lineno, index, line);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(opcode_to_byte(Opcode::GETARG), out);
            fputc(blackbox::tools::parse_register(reg, lineno), out);
            blackbox::data::write_u32(out, idx);
        } else if (blackbox::tools::starts_with_ci(s, "GETENV")) {
            char reg[16];
            char varname[256];
            char* comma = strchr(s + 6, ',');
            if (!comma || sscanf(s + 6, " %15[^,]", reg) != 1) {
                std::println(stderr,
                             "Syntax error on line {}: expected GETENV <register>, <varname>\nGot: {}",
                             lineno, line);
                fclose(in);
                fclose(out);
                return 1;
            }

            const char* p = comma + 1;
            while (*p && isspace(static_cast<unsigned char>(*p))) {
                p++;
            }

            if (*p == '"') {
                const char* end = strchr(p + 1, '"');
                if (!end) {
                    std::println(stderr,
                                 "Syntax error on line {}: expected GETENV <register>, \"<varname>\"\nGot: {}",
                                 lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                size_t len = static_cast<size_t>(end - (p + 1));
                if (len >= sizeof(varname)) {
                    len = sizeof(varname) - 1;
                }
                memcpy(varname, p + 1, len);
                varname[len] = '\0';
            } else {
                if (sscanf(p, " %255s", varname) != 1) {
                    std::println(
                        stderr,
                        "Syntax error on line {}: expected GETENV <register>, <varname>\nGot: {}",
                        lineno, line);
                    fclose(in);
                    fclose(out);
                    return 1;
                }
                char* end = varname + strlen(varname) - 1;
                while (end > varname && (*end == '\r' || *end == '\n')) {
                    bbxc::asm_helpers::trim_crlf(varname);
                    break;
                }
            }

            fputc(opcode_to_byte(Opcode::GETENV), out);
            fputc(blackbox::tools::parse_register(reg, lineno), out);
            uint8_t len = static_cast<uint8_t>(strlen(varname));
            fputc(len, out);
            for (size_t i = 0; i < len; i++) {
                fputc(static_cast<uint8_t>(varname[i]), out);
            }
        } else {
            std::println(stderr, "Unknown instruction on line {}:\n {}", lineno, s);
            fclose(in);
            fclose(out);
            return 1;
        }
    }
    fclose(in);
    fclose(out);
    return 0;
}
