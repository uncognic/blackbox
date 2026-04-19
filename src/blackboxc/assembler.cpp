//
// Created by User on 2026-04-19.
//

#include "assembler.hpp"
#include "../define.hpp"
#include "../utils/preprocessor.hpp"
#include "encoder.hpp"
#include "utils/string_utils.hpp"

#include <format>
#include <fstream>
#include <print>

using namespace bbxc::asm_helpers;
using namespace bbxc::encoder;

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

    // get raw lines
    std::vector<std::string> raw;
    collect_lines_from_buffer(preprocessed, raw);

    // collect macros
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

    for (size_t i = 0; i < raw.size(); i++) {
        std::string t = trim_copy(raw[i]);

        if (blackbox::tools::starts_with_ci(t.data(), "%macro")) {
            for (i++; i < raw.size(); i++) {
                if (trim_copy(raw[i]) == "%endmacro") {
                    break;
                }
                continue;
            }

            if (t == "%asm" || t == "%data" || t == "%main" || t == "%entry" || t == "%endmacro") {
                lines.push_back(std::string(t));
                continue;
            }

            if (blackbox::tools::starts_with_ci(t.data(), "%globals")) {
                lines.push_back(std::string(t));
                continue;
            }
            if (!t.empty() && t[0] == '%') {
                // TODO: change expand invocation to work with this
            }
            lines.push_back(raw[i]);
        }
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
    enum class Section { None, Data, Code };
    Section section = Section::None;

    // header size: magic(3) + global_count(4) + entry_count(4) = 11
    constexpr uint32_t HEADER = 11;
    uint32_t pc = HEADER;

    for (auto& raw_line : lines) {
        std::string s = trim_copy(raw_line);
        if (s.empty() || s[0] == ';') {
            continue;
        }

        size_t comment = s.find(';');
        if (comment != std::string_view::npos) {
            s = trim_copy(s.substr(0, comment));
        }
        if (s.empty()) {
            continue;
        }

        if (s == "%asm") {
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

        // %globals N
        if (blackbox::tools::starts_with_ci(s.data(), "%globals")) {
            auto tok = trim_copy(s.substr(8));
            uint32_t n = 0;
            auto r = std::from_chars(tok.data(), tok.data() + tok.size(), n);
            if (r.ec != std::errc{}) {
                return std::unexpected(std::format("invalid %globals value: '{}'", tok));
            }
            global_count = n;
            continue;
        }

        if (section == Section::Data) {
            // STR $name, "value"
            if (blackbox::tools::starts_with_ci(s.data(), "STR")) {
                auto rest = trim_copy(s.substr(3));
                // parse $name
                if (rest.empty() || rest[0] != '$') {
                    return std::unexpected(std::format("expected $name in STR: '{}'", s));
                }
                size_t comma = rest.find(',');
                if (comma == std::string_view::npos) {
                    return std::unexpected(std::format("expected comma in STR: '{}'", s));
                }
                std::string name = trim_copy(std::string(rest.substr(1, comma - 1)));
                auto rest2 = trim_copy(rest.substr(comma + 1));

                size_t q1 = rest2.find('"');
                if (q1 == std::string_view::npos) {
                    return std::unexpected(std::format("expected quoted string in STR: '{}'", s));
                }
                size_t q2 = rest2.find('"', q1 + 1);
                if (q2 == std::string_view::npos) {
                    return std::unexpected("unterminated string in STR");
                }
                std::string value = std::string(rest2.substr(q1 + 1, q2 - q1 - 1));

                data_entries.push_back(DataEntry{name, value, static_cast<uint32_t>(data_entries.size())});

                // entry: type(1) + length(4) + bytes
                pc += 1 + 4 + static_cast<uint32_t>(value.size());
            }

            // usually here we would handle DWORDs and stuff
            // but fuck that we are not doing that
            // the assembler can just MOVI it
            continue;
        }

        if (section == Section::Code) {
            // label definition .name:
            if (s[0] == '.') {
                std::string name = std::string(s.substr(1));
                if (!name.empty() && name.back() == ':') {
                    name.pop_back();
                }
                if (find_label(name)) {
                    return std::unexpected(std::format("duplicate label '{}'", name));
                }
                labels.push_back(Label{name, pc, 0});
                continue;
            }
            // FRAME n
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

            pc += static_cast<uint32_t>(instr_size(s));
        }
    }

    if (debug) {
        std::println("[ASM] pass1: {} labels, {} data entries, {} globals", labels.size(),
                     data_entries.size(), global_count);
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
    enum class Section { None, Code };
    Section section = Section::None;
    bool found_code = false;

    for (auto& raw_line : lines) {
        std::string s = trim_copy(raw_line);
        if (s.empty() || s[0] == ';') {
            continue;
        }
        size_t comment = s.find(';');
        if (comment != std::string_view::npos) {
            s = trim_copy(s.substr(0, comment));
        }
        if (s.empty()) {
            continue;
        }

        if (s == "%asm" || s == "%data" || s == "%globals" ||
            blackbox::tools::starts_with_ci(s.data(), "%globals")) {
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

        if (section == Section::Code) {
            if (s[0] == '.') {
                continue;
            }

            if (blackbox::tools::starts_with_ci(s.data(), "FRAME")) {
                continue;
            }

            auto result = encode(s, labels, data_entries, code_buf, debug);
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
    write_u32(header_buf, global_count);
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