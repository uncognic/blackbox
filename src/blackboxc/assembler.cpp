//
// Created by User on 2026-04-19.
//

#include "assembler.hpp"
#include "../define.hpp"
#include "../utils/macro_expansion.hpp"
#include "../utils/preprocessor.hpp"
#include "../utils/string_utils.hpp"
#include "encoder.hpp"
#include <format>
#include <fstream>
#include <print>

using namespace bbxc::asm_helpers;
using namespace bbxc::encoder;

std::optional<uint32_t> Assembler::find_bss_symbol(std::string_view name) const {
    auto it = bss_symbols.find(std::string(name));
    if (it == bss_symbols.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::expected<void, std::string> Assembler::assemble(const std::filesystem::path& input,
                                                     const std::filesystem::path& output,
                                                     bool dbg) {
    Assembler a;
    a.debug = dbg;

    if (auto r = a.preprocess(input); !r) {
        return r;
    }
    if (auto r = a.pass1(); !r) {
        return r;
    }
    if (auto r = a.pass2(output); !r) {
        return r;
    }

    return {};
}

std::expected<void, std::string> Assembler::preprocess(const std::filesystem::path& input) {
    std::string preprocessed;
    if (!blackbox::tools::preprocess_includes(input.string().c_str(), preprocessed)) {
        return std::unexpected(std::format("Failed to preprocess '{}'", input.string()));
    }

    if (!blackbox::tools::preprocess_defines(preprocessed, defines)) {
        return std::unexpected("Failed to preprocess defines");
    }

    std::vector<std::string> raw;
    collect_lines_from_buffer(preprocessed, raw);

    std::vector<Macro> macros;
    for (size_t i = 0; i < raw.size(); i++) {
        std::string t = trim_copy(raw[i]);
        if (!blackbox::tools::starts_with_ci(t.data(), "%macro")) {
            continue;
        }

        std::string header = trim_copy(t.substr(6));
        auto tokens = split_tokens(header);
        if (tokens.empty()) {
            continue;
        }

        std::vector<std::string> params(tokens.begin() + 1, tokens.end());
        std::vector<std::string> body;

        size_t j = i + 1;
        for (; j < raw.size(); j++) {
            if (trim_copy(raw[j]) == "%endmacro") {
                break;
            }
            body.push_back(raw[j]);
        }

        macros.push_back(Macro{tokens[0], std::move(params), std::move(body)});
        i = j;
    }

    unsigned long expand_id = 0;
    for (size_t i = 0; i < raw.size(); i++) {
        std::string t = trim_copy(raw[i]);

        if (blackbox::tools::starts_with_ci(t.data(), "%macro")) {
            for (i++; i < raw.size(); i++) {
                if (trim_copy(raw[i]) == "%endmacro") {
                    break;
                }
            }
            continue;
        }

        if (t == "%asm" || t == "%data" || t == "%main" || t == "%entry" || t == "%endmacro" ||
            t == "%bss") {
            lines.push_back(std::string(t));
            continue;
        }

        if (!t.empty() && t[0] == '%') {
            std::vector<std::string> expanded;
            if (blackbox::tools::expand_invocation(t, macros, expanded, expand_id)) {
                for (auto& el : expanded) {
                    lines.push_back(el);
                }
                continue;
            }
        }

        lines.push_back(raw[i]);
    }
    return {};
}

std::optional<uint32_t> Assembler::find_label(std::string_view name) const {
    for (auto& l : labels) {
        if (l.name == name) {
            return l.addr;
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> Assembler::find_data_entry(std::string_view name) const {
    std::string_view n = name;
    if (!n.empty() && n[0] == '$') {
        n.remove_prefix(1);
    }
    for (auto& e : data_entries) {
        if (e.name == n) {
            return e.index;
        }
    }
    return std::nullopt;
}

std::expected<void, std::string> Assembler::pass1() {
    enum class Section { None, Data, Bss, Code };
    Section section = Section::None;

    uint32_t data_size = 0;
    uint32_t code_pc = 0;

    for (auto& raw_line : lines) {
        std::string s = trim_copy(raw_line);
        if (s.empty() || s[0] == ';') {
            continue;
        }

        size_t comment = s.find(';');
        if (comment != std::string::npos) {
            s = trim_copy(s.substr(0, comment));
        }
        if (s.empty()) {
            continue;
        }

        if (s == "%asm") {
            section = Section::None;
            continue;
        }
        if (s == "%data") {
            section = Section::Data;
            continue;
        }
        if (s == "%main" || s == "%entry") {
            section = Section::Code;
            continue;
        }

        if (s == "%bss") {
            section = Section::Bss;
            continue;
        }

        if (section == Section::Data) {
            auto s_trim = trim_copy(s);

            // if there are not 2 tokens
            size_t sp = s_trim.find_first_of(" \t");
            if (sp == std::string_view::npos) {
                return std::unexpected(std::format("expected string value after name: '{}'", s));
            }

            std::string name = trim_copy(std::string(s_trim.substr(0, sp)));
            auto rest = trim_copy(s_trim.substr(sp));

            if (name.empty()) {
                return std::unexpected(std::format("empty name data: '{}'", s));
            }

            // is value quoted
            size_t q1 = rest.find('"');
            if (q1 == std::string_view::npos) {
                return std::unexpected(std::format("expected quoted string: '{}'", s));
            }

            size_t q2 = rest.find('"', q1 + 1);
            if (q2 == std::string_view::npos) {
                return std::unexpected(std::format("unterminated string: '{}'", s));
            }

            std::string value = std::string(rest.substr(q1 + 1, q2 - q1 - 1));

            uint32_t index = static_cast<uint32_t>(data_entries.size());
            data_entries.push_back(DataEntry{name, value, index});

            // entry: type(1) + length(4) + bytes
            data_size += 1 + 4 + static_cast<uint32_t>(value.size());

            continue;
        }

        if (section == Section::Bss) {
            if (s[0] == '.' || s[0] == '%') {
                continue;
            }

            if (bss_symbols.count(s)) {
                return std::unexpected(std::format("duplicate BSS symbol '{}'", s));
            }
            bss_symbols[s] = bss_count++;
            continue;
        }

        if (section == Section::Code) {
            auto s_trim = trim_copy(s);
            if (!s_trim.empty() && s_trim.back() == ':') {
                std::string name = std::string(s_trim.substr(0, s_trim.size() - 1));

                if (find_label(name)) {
                    return std::unexpected(std::format("duplicate label '{}'", name));
                }

                labels.push_back(Label{name, code_pc, 0});
                continue;
            }
            if (blackbox::tools::starts_with_ci(s.data(), "FRAME")) {
                auto tok = trim_copy(s.substr(5));
                uint32_t fs = 0;
                auto r = std::from_chars(tok.data(), tok.data() + tok.size(), fs);
                if (r.ec != std::errc{}) {
                    return std::unexpected(std::format("invalid FRAME value: '{}'", tok));
                }
                if (!labels.empty()) {
                    labels.back().frame_size = fs;
                }
                continue;
            }
            bbxc::encoder::OperandContext size_ctx{
                .labels = labels,
                .data_entries = data_entries,
                .bss_symbols = bss_symbols,
            };
            uint32_t size = static_cast<uint32_t>(instr_size(s, size_ctx));
            code_pc += size;
            if (debug) {
                std::println("[DBG] Line {}: {} bytes", trim_copy(raw_line), size);
            }
        }
    }

    if (debug) {
        std::println("[ASM] pass1: {} labels, {} data entries, {} bss entries, {} code bytes",
                     labels.size(), data_entries.size(), bss_count, code_pc);
    }

    return {};
}

std::expected<void, std::string> Assembler::pass2(const std::filesystem::path& output) {
    std::vector<uint8_t> code_buf;
    std::vector<uint8_t> data_buf;

    // each entry: type(1) + length(4) + bytes
    for (auto& entry : data_entries) {
        write_u8(data_buf, static_cast<uint8_t>(DataEntryType::String));
        write_u32(data_buf, static_cast<uint32_t>(entry.value.size()));
        for (char c : entry.value) {
            write_u8(data_buf, static_cast<uint8_t>(c));
        }
    }

    // encode code
    enum class Section { None, Bss, Code };
    Section section = Section::None;
    bool found_code = false;

    for (auto& raw_line : lines) {
        std::string s = trim_copy(raw_line);
        if (s.empty() || s[0] == ';') {
            continue;
        }

        size_t comment = s.find(';');
        if (comment != std::string::npos) {
            s = trim_copy(s.substr(0, comment));
        }
        if (s.empty()) {
            continue;
        }

        if (s == "%asm" || s == "%data" || s == "%bss") {
            continue;
        }

        if (s == "%main" || s == "%entry") {
            section = Section::Code;
            found_code = true;
            continue;
        }

        if (s[0] == '%') {
            continue;
        }

        if (s == "%bss") {
            section = Section::Bss;
            continue;
        }
        if (section == Section::Bss) {
            continue;
        }

        if (section == Section::Code) {
            auto s_trim = trim_copy(s);
            if (!s_trim.empty() && s_trim.back() == ':') {
                continue;
            }

            if (blackbox::tools::starts_with_ci(s.data(), "FRAME")) {
                continue;
            }

            OperandContext ctx{labels, data_entries, bss_symbols};
            auto result = encode(s, ctx, code_buf, debug);
            if (!result) {
                return std::unexpected(result.error());
            }
        }
    }
    if (!found_code) {
        return std::unexpected("missing %main or %entry section");
    }

    // write binary
    std::ofstream out(output, std::ios::binary);
    if (!out) {
        return std::unexpected(std::format("failed to open output '{}'", output.string()));
    }

    // header: magic(3) global_count(4) entry_count(4)
    out.put(static_cast<char>((MAGIC >> 16) & 0xFF));
    out.put(static_cast<char>((MAGIC >> 8) & 0xFF));
    out.put(static_cast<char>((MAGIC) & 0xFF));

    std::vector<uint8_t> header_buf;
    write_u32(header_buf, bss_count);
    write_u32(header_buf, static_cast<uint32_t>(data_entries.size()));
    out.write(reinterpret_cast<const char*>(header_buf.data()),
              static_cast<std::streamsize>(header_buf.size()));

    // data section
    out.write(reinterpret_cast<const char*>(data_buf.data()),
              static_cast<std::streamsize>(data_buf.size()));

    // code section
    out.write(reinterpret_cast<const char*>(code_buf.data()),
              static_cast<std::streamsize>(code_buf.size()));

    if (debug) {
        std::println("[ASM] pass2: {} data bytes, {} code bytes", data_buf.size(), code_buf.size());
    }

    return {};
}