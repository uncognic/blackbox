#include "symbol_table.hpp"
#include "string_utils.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>


namespace blackbox {
namespace tools {

uint32_t find_label(const std::string& name, Label* labels, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (name == labels[i].name) {
            return labels[i].addr;
        }
    }
    fprintf(stderr, "Unknown label %s\n", name.c_str());
    exit(1);
}

uint32_t find_data(const std::string& name, Data* data, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (equals_ci(data[i].name, name.c_str())) {
            return i;
        }
    }
    fprintf(stderr, "Error: undefined string constant '%s'\n", name.c_str());
    exit(1);
}

} // namespace tools
} // namespace blackbox
