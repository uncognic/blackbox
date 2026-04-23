//
// Created by User on 2026-04-22.
//

#ifndef BLACKBOX_BASIC_HPP
#define BLACKBOX_BASIC_HPP
#include <filesystem>
#include <optional>
#include <string>

// returns nullopt on success, error string on failure
std::optional<std::string> preprocess_basic(const std::filesystem::path& input,
                                            const std::filesystem::path& output, bool debug);
#endif // BLACKBOX_BASIC_HPP
