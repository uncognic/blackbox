#include "asm_parser.hpp"
#include "../define.hpp"
#include "string_utils.hpp"
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string>

namespace blackbox {
namespace tools {

static inline unsigned char ascii_upper(unsigned char c) {
    return static_cast<unsigned char>(toupper(c));
}

static std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }

    size_t end = s.size();
    while (end > start &&
           (isspace(static_cast<unsigned char>(s[end - 1])) || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        end--;
    }

    return s.substr(start, end - start);
}

static bool second_operand_is_reg(const std::string& line, size_t op_len) {
    if (line.size() <= op_len) {
        return false;
    }
    size_t comma = line.find(',', op_len);
    if (comma == std::string::npos) {
        return false;
    }
    size_t q = comma + 1;
    while (q < line.size() && isspace(static_cast<unsigned char>(line[q]))) {
        q++;
    }
    if (q >= line.size()) {
        return false;
    }
    return ascii_upper(static_cast<unsigned char>(line[q])) == 'R';
}

static size_t quoted_payload_size_or(const std::string& line, size_t fallback) {
    size_t first = line.find('"');
    if (first == std::string::npos) {
        return fallback;
    }
    size_t second = line.find('"', first + 1);
    if (second == std::string::npos) {
        return fallback;
    }
    size_t len = second - (first + 1);
    return len > 255 ? 255 : len;
}

static std::string operand_after_opcode(const std::string& line, size_t op_len) {
    if (line.size() <= op_len) {
        return std::string();
    }
    return trim_copy(line.substr(op_len));
}

size_t instr_size(const char* line) {
    std::string line_sv(line);

    if (starts_with_ci(line, "MOV")) {
        std::string rest = line_sv.size() > 3 ? line_sv.substr(3) : std::string();
        size_t comma = rest.find(',');
        if (comma == std::string::npos) {
            return 0;
        }

        std::string src = trim_copy(rest.substr(comma + 1));
        if (!src.empty() && ascii_upper(static_cast<unsigned char>(src[0])) == 'R') {
            return 3;
        } else {
            return 6;
        }
    } else if (starts_with_ci(line, "PUSH")) {
        return 2;
    } else if (starts_with_ci(line, "PUSHI")) {
        return 5;
    } else if (starts_with_ci(line, "POP")) {
        return 2;
    } else if (starts_with_ci(line, "ADD")) {
        return 3;
    } else if (starts_with_ci(line, "SUB")) {
        return 3;
    } else if (starts_with_ci(line, "MUL")) {
        return 3;
    } else if (starts_with_ci(line, "DIV")) {
        return 3;
    } else if (equals_ci(line, "PRINT_STACKSIZE")) {
        return 1;
    } else if (starts_with_ci(line, "PRINTREG")) {
        return 2;
    } else if (starts_with_ci(line, "EPRINTREG")) {
        return 2;
    } else if (starts_with_ci(line, "PRINT")) {
        return 2;
    } else if (starts_with_ci(line, "WRITE")) {
        return 3 + quoted_payload_size_or(line_sv, 0);
    } else if (starts_with_ci(line, "EXEC")) {
        return 3 + quoted_payload_size_or(line_sv, 0);
    } else if (starts_with_ci(line, "JMPI")) {
        return 5;
    } else if (starts_with_ci(line, "JMP")) {
        return 5;
    } else if (starts_with_ci(line, "ALLOC")) {
        return 5;
    } else if (starts_with_ci(line, "NEWLINE")) {
        return 1;
    } else if (starts_with_ci(line, "JE")) {
        return 5;
    } else if (starts_with_ci(line, "JNE")) {
        return 5;
    } else if (starts_with_ci(line, "INC")) {
        return 2;
    } else if (starts_with_ci(line, "DEC")) {
        return 2;
    } else if (starts_with_ci(line, "CMP")) {
        return 3;
    } else if (starts_with_ci(line, "STORE")) {
        if (!strchr(line + 5, ',')) {
            return 6;
        }
        if (second_operand_is_reg(line_sv, 5)) {
            return 3;
        }
        return 6;
    } else if (starts_with_ci(line, "LOADSTR")) {
        return 6;
    } else if (starts_with_ci(line, "LOADBYTE")) {
        return 6;
    } else if (starts_with_ci(line, "LOADWORD")) {
        return 6;
    } else if (starts_with_ci(line, "LOADDWORD")) {
        return 6;
    } else if (starts_with_ci(line, "LOADQWORD")) {
        return 6;
    } else if (starts_with_ci(line, "LOAD")) {
        if (!strchr(line + 4, ',')) {
            return 6;
        }
        if (second_operand_is_reg(line_sv, 4)) {
            return 3;
        }
        return 6;
    }

    else if (starts_with_ci(line, "LOADVAR")) {
        if (!strchr(line + 7, ',')) {
            return 6;
        }
        if (second_operand_is_reg(line_sv, 7)) {
            return 3;
        }
        return 6;
    } else if (starts_with_ci(line, "GROW")) {
        return 5;
    }

    else if (starts_with_ci(line, "STOREVAR")) {
        if (!strchr(line + 8, ',')) {
            return 6;
        }
        if (second_operand_is_reg(line_sv, 8)) {
            return 3;
        }
        return 6;
    } else if (starts_with_ci(line, "RESIZE")) {
        return 5;
    } else if (starts_with_ci(line, "FREE")) {
        return 5;
    } else if (starts_with_ci(line, "MOD")) {
        return 3;
    } else if (starts_with_ci(line, "FOPEN")) {
        return 4 + quoted_payload_size_or(line_sv, 0);
    } else if (starts_with_ci(line, "FCLOSE")) {
        return 2;
    } else if (starts_with_ci(line, "FREAD")) {
        return 3;
    } else if (starts_with_ci(line, "FWRITE")) {
        if (!strchr(line + 6, ',')) {
            return 6;
        }
        if (second_operand_is_reg(line_sv, 6)) {
            return 3;
        } else {
            return 6;
        }
    } else if (starts_with_ci(line, "FSEEK")) {
        if (!strchr(line + 5, ',')) {
            return 6;
        }
        if (second_operand_is_reg(line_sv, 5)) {
            return 3;
        } else {
            return 6;
        }
    } else if (starts_with_ci(line, "PRINTSTR")) {
        return 2;
    } else if (starts_with_ci(line, "EPRINTSTR")) {
        return 2;
    } else if (starts_with_ci(line, "HALT")) {
        std::string operand = operand_after_opcode(line_sv, 4);
        if (!operand.empty()) {
            return 2;
        } else {
            return 1;
        }
    } else if (starts_with_ci(line, "NOT")) {
        return 2;
    } else if (starts_with_ci(line, "AND")) {
        return 3;
    } else if (starts_with_ci(line, "OR")) {
        return 3;
    } else if (starts_with_ci(line, "XOR")) {
        return 3;
    } else if (starts_with_ci(line, "READSTR")) {
        return 2;
    } else if (starts_with_ci(line, "READCHAR")) {
        return 2;
    } else if (starts_with_ci(line, "SLEEP")) {
        std::string operand = operand_after_opcode(line_sv, 5);
        if (!operand.empty() && ascii_upper(static_cast<unsigned char>(operand[0])) == 'R') {
            return 2; // opcode + register
        }
        return 5; // opcode + u32 immediate
    } else if (starts_with_ci(line, "CLRSCR")) {
        return 1;
    } else if (starts_with_ci(line, "RAND")) {
        return 18;
    } else if (starts_with_ci(line, "GETKEY")) {
        return 2;
    } else if (starts_with_ci(line, "READ")) {
        return 2;
    } else if (equals_ci(line, "CONTINUE")) {
        return 1;
    } else if (starts_with_ci(line, "JL")) {
        return 5;
    } else if (starts_with_ci(line, "JGE")) {
        return 5;
    } else if (starts_with_ci(line, "JB")) {
        return 5;
    } else if (starts_with_ci(line, "JAE")) {
        return 5;
    } else if (starts_with_ci(line, "CALL")) {
        return 9;
    } else if (starts_with_ci(line, "RET")) {
        return 1;
    } else if (starts_with_ci(line, "BREAK")) {
        return 1;
    } else if (starts_with_ci(line, "REGSYSCALL")) {
        return 6;
    } else if (starts_with_ci(line, "SYSCALL")) {
        return 2;
    } else if (starts_with_ci(line, "SYSRET")) {
        return 1;
    } else if (starts_with_ci(line, "DROPPRIV")) {
        return 1;
    } else if (starts_with_ci(line, "GETMODE")) {
        return 2;
    } else if (starts_with_ci(line, "SETPERM")) {
        return 13;
    } else if (starts_with_ci(line, "REGFAULT")) {
        return 6;
    } else if (starts_with_ci(line, "FAULTRET")) {
        return 1;
    } else if (starts_with_ci(line, "GETFAULT")) {
        return 2;
    } else if (starts_with_ci(line, "DUMPREGS")) {
        return 1;
    } else if (starts_with_ci(line, "PRINTCHAR")) {
        return 2;
    } else if (starts_with_ci(line, "EPRINTCHAR")) {
        return 2;
    } else if (starts_with_ci(line, "SHL")) {
        return 3; // opcode + reg + reg
    } else if (starts_with_ci(line, "SHR")) {
        return 3; // opcode + reg + reg
    } else if (starts_with_ci(line, "SHLI")) {
        return 10; // opcode + reg + u64 immediate
    } else if (starts_with_ci(line, "SHRI")) {
        return 10; // opcode + reg + u64 immediate
    } else if (starts_with_ci(line, "GETARG")) {
        return 6; // opcode + reg + u32 index
    } else if (starts_with_ci(line, "GETARGC")) {
        return 2; // opcode + reg
    } else if (starts_with_ci(line, "GETENV")) {
        size_t comma = line_sv.find(',', 6);
        if (comma == std::string::npos) {
            return 3;
        }

        std::string operand = trim_copy(line_sv.substr(comma + 1));
        if (operand.empty()) {
            return 3;
        }

        size_t len = 0;
        if (operand[0] == '"') {
            size_t end = operand.find('"', 1);
            if (end == std::string::npos) {
                return 3;
            }
            len = end - 1;
        } else {
            while (len < operand.size() && operand[len] != '\r' && operand[len] != '\n' &&
                   !isspace(static_cast<unsigned char>(operand[len]))) {
                len++;
            }
        }
        if (len > 255) {
            len = 255;
        }
        return 3 + len;
    } else if (starts_with_ci(line, "LOADREF")) {
        return 3;
    } else if (starts_with_ci(line, "STOREREF")) {
        return 3;
    }
    std::println(stderr, "Unknown instruction for size calculation: {}", line);
    exit(1);
}

uint8_t parse_register(const std::string& r, int lineno) {
    if (r.empty() || ascii_upper(static_cast<unsigned char>(r[0])) != 'R') {
        std::println(stderr, "Invalid register on line {}", lineno);
        exit(1);
    }

    std::string digits = r.substr(1);
    if (digits.empty()) {
        std::println(stderr, "Invalid register on line {}", lineno);
        exit(1);
    }

    int value = 0;
    auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc() || ptr != digits.data() + digits.size() || value < 0 ||
        value >= REGISTERS) {
        std::println(stderr, "Invalid register on line {}", lineno);
        exit(1);
    }

    return static_cast<uint8_t>(value);
}

uint8_t parse_file(const std::string& r, int lineno) {
    if (r.empty() || ascii_upper(static_cast<unsigned char>(r[0])) != 'F') {
        std::println(stderr, "Invalid file descriptor on line {}", lineno);
        exit(1);
    }

    std::string digits = r.substr(1);
    if (digits.empty()) {
        std::println(stderr, "Invalid file descriptor on line {}", lineno);
        exit(1);
    }

    int value = 0;
    auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc() || ptr != digits.data() + digits.size() || value < 0 ||
        value >= FILE_DESCRIPTORS) {
        std::println(stderr, "Invalid file descriptor on line {}", lineno);
        exit(1);
    }

    return static_cast<uint8_t>(value);
}

} // namespace tools
} // namespace blackbox
