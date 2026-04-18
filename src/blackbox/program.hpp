//
// Created by User on 2026-04-18.
//

#ifndef BLACKBOX_PROGRAM_HPP
#define BLACKBOX_PROGRAM_HPP

#include "string_table.hpp"
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

struct Program {
    std::vector<uint8_t> code; // bytecode without header
    StringTable strings;
    uint32_t global_count; // for globals (.bss)
    size_t entry_point = 0; // program_counter for code start

    static std::expected<Program, std::string> load(const std::filesystem::path& path);
};

#endif //BLACKBOX_PROGRAM_HPP
