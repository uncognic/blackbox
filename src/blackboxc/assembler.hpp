//
// Created by User on 2026-04-19.
//

#ifndef BLACKBOX_ASSEMBLER_HPP
#define BLACKBOX_ASSEMBLER_HPP
#include "asm_util.hpp"
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Assembler {
public:
    static std::expected<void, std::string> assemble(const std::filesystem::path& input, const std::filesystem::path& output, bool debug = false);

private:
    std::vector<bbxc::asm_helpers::Label> labels;
    std::vector<bbxc::asm_helpers::DataEntry> data_entries;
    std::unordered_map<std::string, uint32_t> bss_symbols;
    std::vector<std::string> lines;
    std::unordered_map<std::string, std::string> defines;
    uint32_t bss_count = 0;
    bool debug = false;

    std::expected<void, std::string> preprocess(const std::filesystem::path& input);
    std::expected<void, std::string> pass1();
    std::expected<void, std::string> pass2(const std::filesystem::path& output);

    std::optional<uint32_t> find_label(std::string_view name) const;
    std::optional<uint32_t> find_data_entry(std::string_view name) const;
    std::optional<uint32_t> find_bss_symbol(std::string_view name) const;
};


#endif //BLACKBOX_ASSEMBLER_HPP
