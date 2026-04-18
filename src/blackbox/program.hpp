#pragma once

#include "../define.hpp"
#include "string_table.hpp"
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

struct Program {
    std::vector<uint8_t> code;
    StringTable strings;
    uint32_t global_count = 0;
    size_t entry_point    = 0;

    static std::expected<Program, std::string> load(const std::filesystem::path& path);
};