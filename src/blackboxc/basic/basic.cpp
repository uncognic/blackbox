//
// Created by User on 2026-04-22.
//

#include "basic.hpp"
#include "bbx_codegen.hpp"
#include "parser.hpp"
#include <format>
#include <fstream>
#include <print>

std::optional<std::string> preprocess_basic(const std::filesystem::path& input,
                                            const std::filesystem::path& output, bool debug) {
    basic::BlackboxCodeGen cg;
    basic::Parser parser(cg, debug);

    if (auto err = parser.compile_file(input)) {
        return err;
    }

    std::ofstream out(output);
    if (!out) {
        return std::format("failed to open output '{}'", output.string());
    }

    std::string data = cg.get_data_section() + parser.get_additional_data_section();
    std::string code = cg.get_code_section();
    std::string ns_init_code = parser.get_namespace_init_code_section();

    out << "%asm\n";

    auto global_names = parser.get_global_names();

    if (!global_names.empty()) {
        out << "%bss\n";
        for (const auto& name : global_names) {
            out << "    " << name << "\n";
        }
    }

    if (!data.empty()) {
        out << "%data\n";
        out << data;
    }

    out << "%main\n";
    out << "    CALL __bbx_basic_main\n";
    out << "    HALT OK\n";

    uint32_t local_slots = parser.get_local_slot_count();

    if (parser.has_entry_point()) {
        size_t first_newline = code.find('\n');
        if (first_newline != std::string::npos) {
            out << code.substr(0, first_newline + 1);
            out << "    FRAME " << local_slots << "\n";
            out << ns_init_code;
            out << code.substr(first_newline + 1);
        } else {
            out << code;
        }
    } else {
        out << ".__bbx_basic_main:\n";
        out << "    FRAME " << local_slots << "\n";
        out << ns_init_code;
        out << code;
    }

    out << "    RET\n";
    out << parser.get_function_code_section();

    if (debug) {
        std::println("[BASIC] emitted {} data bytes, {} code bytes", data.size(), code.size());
    }

    return std::nullopt;
}
