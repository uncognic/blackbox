#include "symbol_table.hpp"
#include "string_utils.hpp"
#include <cstdio>
#include <cstdlib>
#include <print>


namespace blackbox::tools {

uint32_t find_label(const std::string& name, const Label* labels, const size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (name == labels[i].name) {
            return labels[i].addr;
        }
    }
    std::println(stderr, "Unknown label {}", name);
    exit(1);
}

uint32_t find_data(const std::string& name, const Data* data, const size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (equals_ci(data[i].name, name.c_str())) {
            return i;
        }
    }
    std::println(stderr, "Error: undefined string constant '{}'", name);
    exit(1);
}

} 
