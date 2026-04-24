#include "program.hpp"
#include "../define.hpp"
#include <format>
#include <fstream>

static bool validate_magic(const std::vector<uint8_t>& raw) {
    return raw.size() >= MAGIC_SIZE && raw[0] == ((MAGIC >> 16) & 0xFF) &&
           raw[1] == ((MAGIC >> 8) & 0xFF) && raw[2] == ((MAGIC) & 0xFF);
}

static uint32_t read_u32(const std::vector<uint8_t>& raw, size_t offset) {
    return static_cast<uint32_t>(raw[offset]) | (static_cast<uint32_t>(raw[offset + 1]) << 8) |
           (static_cast<uint32_t>(raw[offset + 2]) << 16) |
           (static_cast<uint32_t>(raw[offset + 3]) << 24);
}

static std::expected<std::vector<uint8_t>, std::string>
read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(std::format("Failed to open '{}'", path.string()));
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>{});
}

static std::expected<Program, std::string> parse(const std::vector<uint8_t>& raw) {
    if (!validate_magic(raw)) {
        return std::unexpected("Invalid magic bytes");
    }

    if (raw.size() < HEADER_FIXED_SIZE) {
        return std::unexpected("File too small: incomplete header");
    }

    // header layout
    // magic(3), bss_count(4), entry_count(4)
    size_t cursor = MAGIC_SIZE;
    uint32_t bss_count = read_u32(raw, cursor);
    cursor += 4;
    uint32_t entry_count = read_u32(raw, cursor);
    cursor += 4;

    Program prog;
    prog.bss_count = bss_count;

    // parse typed data entries
    // each entry: type(1), length(4), bytes(length)
    for (uint32_t i = 0; i < entry_count; i++) {
        if (cursor + 5 > raw.size()) {
            return std::unexpected(
                std::format("Entry {} header truncated at offset {}", i, cursor));
        }

        auto entry_type = static_cast<DataEntryType>(raw[cursor++]);
        uint32_t length = read_u32(raw, cursor);
        cursor += 4;

        if (cursor + length > raw.size()) {
            return std::unexpected(std::format("Entry {} data truncated at offset {}", i, cursor));
        }

        switch (entry_type) {
            case DataEntryType::String: {
                std::string_view s(reinterpret_cast<const char*>(raw.data() + cursor), length);
                uint32_t handle = prog.strings.intern(s);
                prog.data_string_handles.push_back(handle);
                break;
            }
            default:
                return std::unexpected(std::format("Unknown data entry type 0x{:02X} at offset {}",
                                                   static_cast<uint8_t>(entry_type), cursor - 5));
        }

        cursor += length;
    }

    prog.code.assign(raw.begin() + static_cast<ptrdiff_t>(cursor), raw.end());
    prog.entry_point = 0;

    return prog;
}

std::expected<Program, std::string> Program::load(const std::filesystem::path& path) {
    auto raw = read_file(path);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    return parse(*raw);
}