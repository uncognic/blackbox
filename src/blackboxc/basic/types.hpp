//
// Created by User on 2026-04-21.
//

#ifndef BLACKBOX_TYPES_HPP
#define BLACKBOX_TYPES_HPP
#include <cstdint>
#include <string>
#include <vector>

namespace basic {

constexpr int SCRATCH_MIN = 1; // R00 reserved for return value
constexpr int SCRATCH_MAX = 15;
constexpr int SCRATCH_COUNT = 15;

enum class VarType { Int, Str };

struct Variable {
    std::string name;
    VarType type = VarType::Int;
    bool is_const = false;
    bool is_ref = false;
    bool is_global = false;
    uint32_t slot = 0;
    std::string data_name; // only for strs
};

struct RegAlloc {
    uint32_t used = 0;
};

enum class BlockKind { If, While, For };

struct Block {
    BlockKind kind;
    std::string end_label;
    std::string loop_label;
    std::string else_label;
    bool has_else = false;

    // FOR
    uint32_t for_var_slot = 0;
    uint32_t for_limit_slot = 0;
    uint32_t for_step_slot = 0;
    std::string for_var_name;
};

struct FileHandle {
    std::string name;
    uint8_t fd = 0;
};

struct ArrayInfo {
    size_t base = 0;
    size_t length = 0;
};

struct FuncDef;
struct NamespaceDef;

} // namespace basic
#endif // BLACKBOX_TYPES_HPP
