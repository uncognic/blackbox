//
// Created by User on 2026-04-18.
//

#ifndef BLACKBOX_STRING_TABLE_HPP
#define BLACKBOX_STRING_TABLE_HPP

#include <cstdint>
#include <string_view>
#include <vector>
#include <stdexcept>

class StringTable {
public:
    StringTable() {
        buf.reserve(4096);
    }

    // returns a stable handle (offset into buf)
    uint32_t intern(std::string_view s) {
        if (buf.size() + s.size() + 1 > UINT32_MAX) {
            throw std::overflow_error("StringTable: buffer exceeded 4GB");
        }
        uint32_t handle = static_cast<uint32_t>(buf.size());
        buf.insert(buf.end(), s.begin(), s.end());
        buf.push_back('\0');
        return handle;
    }

    // returns a view into the string at handle
    std::string_view get(uint32_t handle) const {
        if (!valid(handle)) {
            throw std::out_of_range("StringTable: invalid handle");
        }
        return std::string_view(buf.data() + handle);
    }

    bool valid(uint32_t handle) const {
        return static_cast<uint32_t>(handle) < buf.size();
    }

    size_t byte_size() const {
        return buf.size();
    }

private:
    std::vector<char> buf;
};
#endif //BLACKBOX_STRING_TABLE_HPP
