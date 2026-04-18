//
// Created by User on 2026-04-18.
//

#include "program.hpp"
#include "../define.hpp"
#include <format>
#include <fstream>

std::expected<Program, std::string> Program::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Failed to open file: {}", path.string()));
    }

    std::vector<uint8_t> raw(std::istreambuf_iterator<char>(file), {});

    // validate magic
    if (raw.size() < 3) {
        return std::unexpected("File too small: missing magic");
    }
    if (raw[0] != ((MAGIC >> 16) & 0xFF) || raw[1] != ((MAGIC >> 8) & 0xFF) ||
        raw[2] != ((MAGIC) & 0xFF)) {
        return std::unexpected("Invalid magic bytes");
    }

    // parse header
    // magic(3), global_count(4), data_count(1), data_table_size(4)
    constexpr size_t HEADER_MIN = 3 + 4 + 1 + 4 // avoid having to change it in 99999 places like last time
    if (raw.size() < HEADER_MIN) {
        return std::unexpected("File too small: header incomplete");
    }

    size_t cursor = 3; // skip magic


}