#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <print>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../data.h"
#include "../define.h"

namespace {

enum class DataRefKind {
    Str,
    Byte,
    Word,
    Dword,
    Qword,
};

struct DataEntry {
    uint32_t offset;
    std::string name;
    DataRefKind kind;
};

struct ParsedImage {
    std::vector<uint8_t> code;
    std::vector<uint8_t> data_table;
    size_t code_start = 0;
    uint8_t data_count = 0;
    bool has_header = false;
};

struct Options {
    bool dump = false;
    bool decomp = false;
    std::string input_path = "out.bcx";
    std::optional<std::string> output_path;
};

struct ScanContext {
    std::set<uint32_t> jump_targets;
    std::map<uint32_t, DataRefKind> data_refs;
    std::map<uint32_t, uint32_t> call_frames;
};

std::string reg_name(uint8_t reg) {
    std::ostringstream oss;
    oss << "R" << std::setw(2) << std::setfill('0') << static_cast<unsigned>(reg);
    return oss.str();
}

std::string file_name(uint8_t fd) {
    std::ostringstream oss;
    oss << "F" << static_cast<unsigned>(fd);
    return oss.str();
}

std::string label_name(uint32_t addr) {
    std::ostringstream oss;
    oss << "L_" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << addr;
    return oss.str();
}

std::string data_name_for_offset(const std::vector<DataEntry>& entries, uint32_t offset) {
    for (const DataEntry& entry : entries) {
        if (entry.offset == offset) {
            return "$" + entry.name;
        }
    }
    std::ostringstream oss;
    oss << "$unknown_" << offset;
    return oss.str();
}

std::string quoted_string(const std::vector<uint8_t>& bytes, size_t index, size_t len) {
    if (index >= bytes.size()) {
        return {};
    }
    const size_t safe_len = std::min(len, bytes.size() - index);
    return std::string(reinterpret_cast<const char*>(&bytes[index]), safe_len);
}

bool parse_image(const std::vector<uint8_t>& raw, ParsedImage& image) {
    image = ParsedImage{};

    if (raw.size() >= HEADER_FIXED_SIZE && raw[0] == 0x62 && raw[1] == 0x63 && raw[2] == 0x78) {
        image.has_header = true;
        image.data_count = raw[3];
        uint32_t data_size = 0;
        if (!blackbox::data::read_u32_le(raw, 4, data_size)) {
            return false;
        }

        const size_t code_start = HEADER_FIXED_SIZE + static_cast<size_t>(data_size);
        if (code_start > raw.size()) {
            return false;
        }

        image.code_start = code_start;
        image.data_table.assign(raw.begin() + static_cast<std::ptrdiff_t>(HEADER_FIXED_SIZE),
                                raw.begin() + static_cast<std::ptrdiff_t>(code_start));
        image.code.assign(raw.begin() + static_cast<std::ptrdiff_t>(code_start), raw.end());
        return true;
    }

    image.code_start = 0;
    image.code = raw;
    return true;
}

std::optional<size_t> scan_one(const std::vector<uint8_t>& code, size_t i, size_t code_start,
                               ScanContext& ctx) {
    if (i >= code.size()) {
        return std::nullopt;
    }

    const uint8_t opcode = code[i];
    size_t j = i + 1;

    auto read_len_prefixed = [&]() -> bool {
        uint8_t len = 0;
        if (!blackbox::data::read_u8(code, j, len)) {
            return false;
        }
        j += 1 + static_cast<size_t>(len);
        return j <= code.size();
    };

    switch (opcode) {
        case OPCODE_WRITE: {
            if (j + 2 > code.size()) {
                return std::nullopt;
            }
            const uint8_t slen = code[j + 1];
            j += 2 + static_cast<size_t>(slen);
            break;
        }
        case OPCODE_EXEC: {
            if (j + 2 > code.size()) {
                return std::nullopt;
            }
            const uint8_t slen = code[j + 1];
            j += 2 + static_cast<size_t>(slen);
            break;
        }
        case OPCODE_NEWLINE: // the reason all of the following opcodes are grouped together is that
                             // they have the same operand format
        case OPCODE_CLRSCR:
        case OPCODE_CONTINUE:
        case OPCODE_BREAK:
        case OPCODE_PRINT_STACKSIZE:
        case OPCODE_RET:
        case OPCODE_SYSRET:
        case OPCODE_DROPPRIV:
        case OPCODE_FAULTRET:
        case OPCODE_DUMPREGS:
            break;
        case OPCODE_PRINT:
        case OPCODE_PUSH_REG:
        case OPCODE_POP:
        case OPCODE_PRINTREG:
        case OPCODE_EPRINTREG:
        case OPCODE_INC:
        case OPCODE_DEC:
        case OPCODE_PRINTSTR:
        case OPCODE_EPRINTSTR:
        case OPCODE_PRINTCHAR:
        case OPCODE_EPRINTCHAR:
        case OPCODE_READ:
        case OPCODE_READSTR:
        case OPCODE_READCHAR:
        case OPCODE_GETKEY:
        case OPCODE_NOT:
        case OPCODE_SYSCALL:
        case OPCODE_GETMODE:
        case OPCODE_GETFAULT:
        case OPCODE_GETARGC:
        case OPCODE_SLEEP_REG:
            j += 1;
            break;
        case OPCODE_PUSHI:
        case OPCODE_ALLOC:
        case OPCODE_GROW:
        case OPCODE_RESIZE:
        case OPCODE_FREE:
        case OPCODE_SLEEP:
        case OPCODE_JMP:
        case OPCODE_JMPI:
        case OPCODE_JE:
        case OPCODE_JNE:
        case OPCODE_JL:
        case OPCODE_JGE:
        case OPCODE_JB:
        case OPCODE_JAE:
            if (opcode == OPCODE_JMP || opcode == OPCODE_JMPI || opcode == OPCODE_JE ||
                opcode == OPCODE_JNE || opcode == OPCODE_JL || opcode == OPCODE_JGE ||
                opcode == OPCODE_JB || opcode == OPCODE_JAE) {
                uint32_t addr = 0;
                if (blackbox::data::read_u32_le(code, j, addr)) {
                    ctx.jump_targets.insert(addr);
                }
            }
            j += 4;
            break;
        case OPCODE_MOVI:
            j += 1 + 4;
            break;
        case OPCODE_MOV_REG:
        case OPCODE_ADD:
        case OPCODE_SUB:
        case OPCODE_MUL:
        case OPCODE_DIV:
        case OPCODE_MOD:
        case OPCODE_XOR:
        case OPCODE_AND:
        case OPCODE_OR:
        case OPCODE_CMP:
        case OPCODE_LOAD_REG:
        case OPCODE_STORE_REG:
        case OPCODE_LOADVAR_REG:
        case OPCODE_STOREVAR_REG:
        case OPCODE_FREAD:
        case OPCODE_FWRITE_REG:
        case OPCODE_FSEEK_REG:
        case OPCODE_SHL:
        case OPCODE_SHR:
            j += 2;
            break;
        case OPCODE_LOAD:
        case OPCODE_STORE:
        case OPCODE_LOADVAR:
        case OPCODE_STOREVAR:
        case OPCODE_FWRITE_IMM:
        case OPCODE_FSEEK_IMM:
        case OPCODE_GETARG:
            j += 1 + 4;
            break;
        case OPCODE_LOADSTR:
        case OPCODE_LOADBYTE:
        case OPCODE_LOADWORD:
        case OPCODE_LOADDWORD:
        case OPCODE_LOADQWORD: {
            if (j + 5 <= code.size()) {
                uint32_t off = 0;
                if (blackbox::data::read_u32_le(code, j + 1, off)) {
                    DataRefKind kind = DataRefKind::Str;
                    if (opcode == OPCODE_LOADBYTE) {
                        kind = DataRefKind::Byte;
                    } else if (opcode == OPCODE_LOADWORD) {
                        kind = DataRefKind::Word;
                    } else if (opcode == OPCODE_LOADDWORD) {
                        kind = DataRefKind::Dword;
                    } else if (opcode == OPCODE_LOADQWORD) {
                        kind = DataRefKind::Qword;
                    }
                    ctx.data_refs.emplace(off, kind);
                }
            }
            j += 1 + 4;
            break;
        }
        case OPCODE_FOPEN: {
            if (j + 3 > code.size()) {
                return std::nullopt;
            }
            const uint8_t name_len = code[j + 2];
            j += 3 + static_cast<size_t>(name_len);
            break;
        }
        case OPCODE_RAND:
            j += 1 + 8 + 8;
            break;
        case OPCODE_CALL: {
            uint32_t addr = 0;
            uint32_t frame = 0;
            if (blackbox::data::read_u32_le(code, j, addr)) {
                ctx.jump_targets.insert(addr);
                if (blackbox::data::read_u32_le(code, j + 4, frame) && frame > 0) {
                    ctx.call_frames[addr] = frame;
                }
            }
            j += 8;
            break;
        }
        case OPCODE_HALT:
            if (j < code.size()) {
                j += 1;
            }
            break;
        case OPCODE_REGSYSCALL:
        case OPCODE_REGFAULT: {
            uint32_t addr = 0;
            if (blackbox::data::read_u32_le(code, j + 1, addr)) {
                ctx.jump_targets.insert(addr);
            }
            j += 1 + 4;
            break;
        }
        case OPCODE_SETPERM:
            j += 4 + 4 + 1 + 1 + 1 + 1;
            break;
        case OPCODE_SHLI:
        case OPCODE_SHRI:
            j += 1 + 8;
            break;
        case OPCODE_GETENV: {
            if (j + 2 > code.size()) {
                return std::nullopt;
            }
            const uint8_t len = code[j + 1];
            j += 2 + static_cast<size_t>(len);
            break;
        }
        default:
            break;
    }

    if (j > code.size()) {
        return std::nullopt;
    }
    (void) code_start;
    (void) read_len_prefixed;
    return j;
}

std::vector<DataEntry> reconstruct_data_entries(const std::vector<uint8_t>& data_table,
                                                const std::map<uint32_t, DataRefKind>& data_refs) {
    std::vector<DataEntry> entries;
    size_t index = 0;
    for (const auto& [offset, kind] : data_refs) {
        if (offset >= data_table.size()) {
            continue;
        }

        DataEntry entry{};
        entry.offset = offset;
        entry.kind = kind;

        std::ostringstream name;
        name << "d" << index++;
        entry.name = name.str();
        entries.push_back(entry);
    }
    return entries;
}

void emit_data_section(std::ostream& os, const std::vector<uint8_t>& data_table,
                       const std::vector<DataEntry>& entries) {
    if (entries.empty()) {
        return;
    }

    os << "%data\n";
    for (const DataEntry& entry : entries) {
        const size_t off = static_cast<size_t>(entry.offset);
        switch (entry.kind) {
            case DataRefKind::Str: {
                size_t end = off;
                while (end < data_table.size() && data_table[end] != 0) {
                    end++;
                }
                const std::string s(reinterpret_cast<const char*>(&data_table[off]), end - off);
                os << "    str $" << entry.name << ", \"" << s << "\"\n";
                break;
            }
            case DataRefKind::Byte:
                if (off + 1 <= data_table.size()) {
                    os << "    byte $" << entry.name << ", "
                       << static_cast<unsigned>(data_table[off]) << "\n";
                }
                break;
            case DataRefKind::Word:
                if (off + 2 <= data_table.size()) {
                    const uint16_t v = static_cast<uint16_t>(data_table[off]) |
                                       (static_cast<uint16_t>(data_table[off + 1]) << 8U);
                    os << "    word $" << entry.name << ", " << v << "\n";
                }
                break;
            case DataRefKind::Dword:
                if (off + 4 <= data_table.size()) {
                    const uint32_t v = static_cast<uint32_t>(data_table[off]) |
                                       (static_cast<uint32_t>(data_table[off + 1]) << 8U) |
                                       (static_cast<uint32_t>(data_table[off + 2]) << 16U) |
                                       (static_cast<uint32_t>(data_table[off + 3]) << 24U);
                    os << "    dword $" << entry.name << ", " << v << "\n";
                }
                break;
            case DataRefKind::Qword:
                if (off + 8 <= data_table.size()) {
                    uint64_t v = 0;
                    for (size_t i = 0; i < 8; i++) {
                        v |= static_cast<uint64_t>(data_table[off + i]) << (i * 8U);
                    }
                    os << "    qword $" << entry.name << ", " << v << "\n";
                }
                break;
        }
    }
}

std::optional<size_t> emit_instruction_decomp(std::ostream& os, const std::vector<uint8_t>& code,
                                              size_t i, size_t code_start, const ScanContext& ctx,
                                              const std::vector<DataEntry>& data_entries) {
    if (i >= code.size()) {
        return std::nullopt;
    }

    const uint32_t abs_offset = static_cast<uint32_t>(code_start + i);
    if (ctx.jump_targets.find(abs_offset) != ctx.jump_targets.end()) {
        os << "." << label_name(abs_offset) << ":\n";
        auto frame_it = ctx.call_frames.find(abs_offset);
        if (frame_it != ctx.call_frames.end() && frame_it->second > 0) {
            os << "    frame " << frame_it->second << "\n";
        }
    }

    const uint8_t opcode = code[i];
    size_t j = i + 1;

    auto fail_if_over = [&]() -> std::optional<size_t> {
        if (j > code.size()) {
            return std::nullopt;
        }
        return j;
    };

    switch (opcode) {
        case OPCODE_WRITE: {
            if (j + 2 > code.size()) {
                return std::nullopt;
            }
            const uint8_t fd = code[j];
            const uint8_t slen = code[j + 1];
            j += 2;
            const std::string s = quoted_string(code, j, slen);
            j += std::min(static_cast<size_t>(slen), code.size() - j);
            const char* fd_name = "STDOUT";
            if (fd == 2) {
                fd_name = "STDERR";
            }
            os << "    write " << fd_name << " \"" << s << "\"\n";
            break;
        }
        case OPCODE_EXEC: {
            if (j + 2 > code.size()) {
                return std::nullopt;
            }
            const uint8_t dest = code[j];
            const uint8_t slen = code[j + 1];
            j += 2;
            const std::string s = quoted_string(code, j, slen);
            j += std::min(static_cast<size_t>(slen), code.size() - j);
            os << "    exec \"" << s << "\", " << reg_name(dest) << "\n";
            break;
        }
        case OPCODE_NEWLINE:
            os << "    newline\n";
            break;
        case OPCODE_PRINT: {
            uint8_t ch = 0;
            if (!blackbox::data::read_u8(code, j, ch)) {
                return std::nullopt;
            }
            j += 1;
            os << "    print '" << static_cast<char>(ch) << "'\n";
            break;
        }
        case OPCODE_PUSHI: {
            int32_t v = 0;
            if (!blackbox::data::read_i32_le(code, j, v)) {
                return std::nullopt;
            }
            j += 4;
            os << "    pushi " << v << "\n";
            break;
        }
        case OPCODE_PUSH_REG: {
            uint8_t r = 0;
            if (!blackbox::data::read_u8(code, j, r)) {
                return std::nullopt;
            }
            j += 1;
            os << "    push " << reg_name(r) << "\n";
            break;
        }
        case OPCODE_POP: {
            uint8_t r = 0;
            if (!blackbox::data::read_u8(code, j, r)) {
                return std::nullopt;
            }
            j += 1;
            os << "    pop " << reg_name(r) << "\n";
            break;
        }
        case OPCODE_MOVI: {
            if (j + 5 > code.size()) {
                return std::nullopt;
            }
            const uint8_t dst = code[j];
            int32_t imm = 0;
            if (!blackbox::data::read_i32_le(code, j + 1, imm)) {
                return std::nullopt;
            }
            j += 5;
            os << "    movi " << reg_name(dst) << ", " << imm << "\n";
            break;
        }
        case OPCODE_MOV_REG:
        case OPCODE_ADD:
        case OPCODE_SUB:
        case OPCODE_MUL:
        case OPCODE_DIV:
        case OPCODE_MOD:
        case OPCODE_XOR:
        case OPCODE_AND:
        case OPCODE_OR:
        case OPCODE_CMP:
        case OPCODE_LOAD_REG:
        case OPCODE_STORE_REG:
        case OPCODE_LOADVAR_REG:
        case OPCODE_STOREVAR_REG:
        case OPCODE_FREAD:
        case OPCODE_FWRITE_REG:
        case OPCODE_FSEEK_REG:
        case OPCODE_SHL:
        case OPCODE_SHR: {
            if (j + 2 > code.size()) {
                return std::nullopt;
            }
            const uint8_t a = code[j];
            const uint8_t b = code[j + 1];
            j += 2;

            switch (opcode) {
                case OPCODE_MOV_REG:
                    os << "    mov " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_ADD:
                    os << "    add " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_SUB:
                    os << "    sub " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_MUL:
                    os << "    mul " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_DIV:
                    os << "    div " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_MOD:
                    os << "    mod " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_XOR:
                    os << "    xor " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_AND:
                    os << "    and " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_OR:
                    os << "    or " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_CMP:
                    os << "    cmp " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_LOAD_REG:
                    os << "    load " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_STORE_REG:
                    os << "    store " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_LOADVAR_REG:
                    os << "    loadvar " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_STOREVAR_REG:
                    os << "    storevar " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_FREAD:
                    os << "    fread " << file_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_FWRITE_REG:
                    os << "    fwrite " << file_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_FSEEK_REG:
                    os << "    fseek " << file_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_SHL:
                    os << "    shl " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                case OPCODE_SHR:
                    os << "    shr " << reg_name(a) << ", " << reg_name(b) << "\n";
                    break;
                default:
                    break;
            }
            break;
        }
        case OPCODE_NOT:
        case OPCODE_PRINTREG:
        case OPCODE_EPRINTREG:
        case OPCODE_INC:
        case OPCODE_DEC:
        case OPCODE_PRINTSTR:
        case OPCODE_EPRINTSTR:
        case OPCODE_READ:
        case OPCODE_READSTR:
        case OPCODE_READCHAR:
        case OPCODE_GETKEY:
        case OPCODE_PRINTCHAR:
        case OPCODE_EPRINTCHAR:
        case OPCODE_SLEEP_REG:
        case OPCODE_GETMODE:
        case OPCODE_GETFAULT:
        case OPCODE_GETARGC: {
            uint8_t r = 0;
            if (!blackbox::data::read_u8(code, j, r)) {
                return std::nullopt;
            }
            j += 1;

            switch (opcode) {
                case OPCODE_NOT:
                    os << "    not " << reg_name(r) << "\n";
                    break;
                case OPCODE_PRINTREG:
                    os << "    printreg " << reg_name(r) << "\n";
                    break;
                case OPCODE_EPRINTREG:
                    os << "    eprintreg " << reg_name(r) << "\n";
                    break;
                case OPCODE_INC:
                    os << "    inc " << reg_name(r) << "\n";
                    break;
                case OPCODE_DEC:
                    os << "    dec " << reg_name(r) << "\n";
                    break;
                case OPCODE_PRINTSTR:
                    os << "    printstr " << reg_name(r) << "\n";
                    break;
                case OPCODE_EPRINTSTR:
                    os << "    eprintstr " << reg_name(r) << "\n";
                    break;
                case OPCODE_READ:
                    os << "    read " << reg_name(r) << "\n";
                    break;
                case OPCODE_READSTR:
                    os << "    readstr " << reg_name(r) << "\n";
                    break;
                case OPCODE_READCHAR:
                    os << "    readchar " << reg_name(r) << "\n";
                    break;
                case OPCODE_GETKEY:
                    os << "    getkey " << reg_name(r) << "\n";
                    break;
                case OPCODE_PRINTCHAR:
                    os << "    printchar " << reg_name(r) << "\n";
                    break;
                case OPCODE_EPRINTCHAR:
                    os << "    eprintchar " << reg_name(r) << "\n";
                    break;
                case OPCODE_SLEEP_REG:
                    os << "    sleep " << reg_name(r) << "\n";
                    break;
                case OPCODE_GETMODE:
                    os << "    getmode " << reg_name(r) << "\n";
                    break;
                case OPCODE_GETFAULT:
                    os << "    getfault " << reg_name(r) << "\n";
                    break;
                case OPCODE_GETARGC:
                    os << "    getargc " << reg_name(r) << "\n";
                    break;
                default:
                    break;
            }
            break;
        }
        case OPCODE_JMP:
        case OPCODE_JE:
        case OPCODE_JNE:
        case OPCODE_JL:
        case OPCODE_JGE:
        case OPCODE_JB:
        case OPCODE_JAE:
        case OPCODE_JMPI: {
            uint32_t addr = 0;
            if (!blackbox::data::read_u32_le(code, j, addr)) {
                return std::nullopt;
            }
            j += 4;

            switch (opcode) {
                case OPCODE_JMP:
                    os << "    jmp " << label_name(addr) << "\n";
                    break;
                case OPCODE_JE:
                    os << "    je " << label_name(addr) << "\n";
                    break;
                case OPCODE_JNE:
                    os << "    jne " << label_name(addr) << "\n";
                    break;
                case OPCODE_JL:
                    os << "    jl " << label_name(addr) << "\n";
                    break;
                case OPCODE_JGE:
                    os << "    jge " << label_name(addr) << "\n";
                    break;
                case OPCODE_JB:
                    os << "    jb " << label_name(addr) << "\n";
                    break;
                case OPCODE_JAE:
                    os << "    jae " << label_name(addr) << "\n";
                    break;
                case OPCODE_JMPI:
                    os << "    jmpi " << addr << "\n";
                    break;
                default:
                    break;
            }
            break;
        }
        case OPCODE_ALLOC:
        case OPCODE_GROW:
        case OPCODE_RESIZE:
        case OPCODE_FREE:
        case OPCODE_SLEEP: {
            uint32_t value = 0;
            if (!blackbox::data::read_u32_le(code, j, value)) {
                return std::nullopt;
            }
            j += 4;

            switch (opcode) {
                case OPCODE_ALLOC:
                    os << "    alloc " << value << "\n";
                    break;
                case OPCODE_GROW:
                    os << "    grow " << value << "\n";
                    break;
                case OPCODE_RESIZE:
                    os << "    resize " << value << "\n";
                    break;
                case OPCODE_FREE:
                    os << "    free " << value << "\n";
                    break;
                case OPCODE_SLEEP:
                    os << "    sleep " << value << "\n";
                    break;
                default:
                    break;
            }
            break;
        }
        case OPCODE_LOAD:
        case OPCODE_STORE:
        case OPCODE_LOADVAR:
        case OPCODE_STOREVAR:
        case OPCODE_LOADSTR:
        case OPCODE_LOADBYTE:
        case OPCODE_LOADWORD:
        case OPCODE_LOADDWORD:
        case OPCODE_LOADQWORD:
        case OPCODE_FWRITE_IMM:
        case OPCODE_FSEEK_IMM:
        case OPCODE_GETARG: {
            if (j + 5 > code.size()) {
                return std::nullopt;
            }
            const uint8_t a = code[j];
            uint32_t b = 0;
            if (!blackbox::data::read_u32_le(code, j + 1, b)) {
                return std::nullopt;
            }
            j += 5;

            switch (opcode) {
                case OPCODE_LOAD:
                    os << "    load " << reg_name(a) << ", " << b << "\n";
                    break;
                case OPCODE_STORE:
                    os << "    store " << reg_name(a) << ", " << b << "\n";
                    break;
                case OPCODE_LOADVAR:
                    os << "    loadvar " << reg_name(a) << ", " << b << "\n";
                    break;
                case OPCODE_STOREVAR:
                    os << "    storevar " << reg_name(a) << ", " << b << "\n";
                    break;
                case OPCODE_LOADSTR:
                    os << "    loadstr " << data_name_for_offset(data_entries, b) << ", "
                       << reg_name(a) << "\n";
                    break;
                case OPCODE_LOADBYTE:
                    os << "    loadbyte " << data_name_for_offset(data_entries, b) << ", "
                       << reg_name(a) << "\n";
                    break;
                case OPCODE_LOADWORD:
                    os << "    loadword " << data_name_for_offset(data_entries, b) << ", "
                       << reg_name(a) << "\n";
                    break;
                case OPCODE_LOADDWORD:
                    os << "    loaddword " << data_name_for_offset(data_entries, b) << ", "
                       << reg_name(a) << "\n";
                    break;
                case OPCODE_LOADQWORD:
                    os << "    loadqword " << data_name_for_offset(data_entries, b) << ", "
                       << reg_name(a) << "\n";
                    break;
                case OPCODE_FWRITE_IMM:
                    os << "    fwrite " << file_name(a) << ", " << static_cast<int32_t>(b) << "\n";
                    break;
                case OPCODE_FSEEK_IMM:
                    os << "    fseek " << file_name(a) << ", " << static_cast<int32_t>(b) << "\n";
                    break;
                case OPCODE_GETARG:
                    os << "    getarg " << reg_name(a) << ", " << b << "\n";
                    break;
                default:
                    break;
            }
            break;
        }
        case OPCODE_FOPEN: {
            if (j + 3 > code.size()) {
                return std::nullopt;
            }
            const uint8_t mode = code[j];
            const uint8_t fd = code[j + 1];
            const uint8_t name_len = code[j + 2];
            j += 3;
            const std::string filename = quoted_string(code, j, name_len);
            j += std::min(static_cast<size_t>(name_len), code.size() - j);

            const char* mode_str = "r";
            if (mode == 1) {
                mode_str = "w";
            } else if (mode == 2) {
                mode_str = "a";
            }

            os << "    fopen " << mode_str << ", " << file_name(fd) << ", \"" << filename << "\"\n";
            break;
        }
        case OPCODE_FCLOSE: {
            uint8_t fd = 0;
            if (!blackbox::data::read_u8(code, j, fd)) {
                return std::nullopt;
            }
            j += 1;
            os << "    fclose " << file_name(fd) << "\n";
            break;
        }
        case OPCODE_RAND: {
            if (j + 17 > code.size()) {
                return std::nullopt;
            }
            const uint8_t reg = code[j];
            uint64_t min_value = 0;
            uint64_t max_value = 0;
            if (!blackbox::data::read_u64_le(code, j + 1, min_value) ||
                !blackbox::data::read_u64_le(code, j + 9, max_value)) {
                return std::nullopt;
            }
            j += 17;

            if (min_value == static_cast<uint64_t>(INT64_MIN) &&
                max_value == static_cast<uint64_t>(INT64_MAX)) {
                os << "    rand " << reg_name(reg) << "\n";
            } else {
                os << "    rand " << reg_name(reg) << ", " << static_cast<int64_t>(min_value)
                   << ", " << static_cast<int64_t>(max_value) << "\n";
            }
            break;
        }
        case OPCODE_CALL: {
            uint32_t addr = 0;
            uint32_t frame = 0;
            if (!blackbox::data::read_u32_le(code, j, addr) ||
                !blackbox::data::read_u32_le(code, j + 4, frame)) {
                return std::nullopt;
            }
            j += 8;
            (void) frame;
            os << "    call " << label_name(addr) << "\n";
            break;
        }
        case OPCODE_RET:
            os << "    ret\n";
            break;
        case OPCODE_PRINT_STACKSIZE:
            os << "    print_stacksize\n";
            break;
        case OPCODE_HALT:
            if (j < code.size()) {
                const uint8_t code_byte = code[j];
                j += 1;
                if (code_byte == 0) {
                    os << "    halt ok\n";
                } else if (code_byte == 1) {
                    os << "    halt bad\n";
                } else {
                    os << "    halt " << static_cast<unsigned>(code_byte) << "\n";
                }
            } else {
                os << "    halt\n";
            }
            break;
        case OPCODE_BREAK:
            os << "    break\n";
            break;
        case OPCODE_CONTINUE:
            os << "    continue\n";
            break;
        case OPCODE_CLRSCR:
            os << "    clrscr\n";
            break;
        case OPCODE_SYSCALL: {
            uint8_t id = 0;
            if (!blackbox::data::read_u8(code, j, id)) {
                return std::nullopt;
            }
            j += 1;
            os << "    syscall " << static_cast<unsigned>(id) << "\n";
            break;
        }
        case OPCODE_REGSYSCALL:
        case OPCODE_REGFAULT: {
            if (j + 5 > code.size()) {
                return std::nullopt;
            }
            const uint8_t id = code[j];
            uint32_t addr = 0;
            if (!blackbox::data::read_u32_le(code, j + 1, addr)) {
                return std::nullopt;
            }
            j += 5;
            if (opcode == OPCODE_REGSYSCALL) {
                os << "    regsyscall " << static_cast<unsigned>(id) << ", " << label_name(addr)
                   << "\n";
            } else {
                os << "    regfault " << static_cast<unsigned>(id) << ", " << label_name(addr)
                   << "\n";
            }
            break;
        }
        case OPCODE_SYSRET:
            os << "    sysret\n";
            break;
        case OPCODE_DROPPRIV:
            os << "    droppriv\n";
            break;
        case OPCODE_SETPERM: {
            if (j + 12 > code.size()) {
                return std::nullopt;
            }
            uint32_t start = 0;
            uint32_t count = 0;
            if (!blackbox::data::read_u32_le(code, j, start) ||
                !blackbox::data::read_u32_le(code, j + 4, count)) {
                return std::nullopt;
            }
            const uint8_t priv_r = code[j + 8];
            const uint8_t priv_w = code[j + 9];
            const uint8_t prot_r = code[j + 10];
            const uint8_t prot_w = code[j + 11];
            j += 12;

            std::string priv;
            if (priv_r) {
                priv += 'R';
            }
            if (priv_w) {
                priv += 'W';
            }
            if (priv.empty()) {
                priv = "-";
            }

            std::string prot;
            if (prot_r) {
                prot += 'R';
            }
            if (prot_w) {
                prot += 'W';
            }
            if (prot.empty()) {
                prot = "-";
            }

            os << "    setperm " << start << ", " << count << ", " << priv << "/" << prot << "\n";
            break;
        }
        case OPCODE_DUMPREGS:
            os << "    dumpregs\n";
            break;
        case OPCODE_FAULTRET:
            os << "    faultret\n";
            break;
        case OPCODE_SHLI:
        case OPCODE_SHRI: {
            if (j + 9 > code.size()) {
                return std::nullopt;
            }
            const uint8_t reg = code[j];
            uint64_t amount = 0;
            if (!blackbox::data::read_u64_le(code, j + 1, amount)) {
                return std::nullopt;
            }
            j += 9;
            if (opcode == OPCODE_SHLI) {
                os << "    shli " << reg_name(reg) << ", " << amount << "\n";
            } else {
                os << "    shri " << reg_name(reg) << ", " << amount << "\n";
            }
            break;
        }
        case OPCODE_GETENV: {
            if (j + 2 > code.size()) {
                return std::nullopt;
            }
            const uint8_t reg = code[j];
            const uint8_t len = code[j + 1];
            j += 2;
            const std::string name = quoted_string(code, j, len);
            j += std::min(static_cast<size_t>(len), code.size() - j);
            os << "    getenv " << reg_name(reg) << ", \"" << name << "\"\n";
            break;
        }
        default:
            os << "    ; unknown opcode 0x" << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<unsigned>(opcode) << std::dec << "\n";
            break;
    }

    return fail_if_over();
}

std::optional<size_t> emit_instruction_dump(std::ostream& os, const std::vector<uint8_t>& bytes,
                                            size_t i) {
    if (i >= bytes.size()) {
        return std::nullopt;
    }

    const size_t offset = i;
    const uint8_t opcode = bytes[i];
    size_t j = i + 1;

    auto print_offset = [&](const std::string& text) {
        std::ostringstream line;
        line << "0x" << std::hex << std::setw(6) << std::setfill('0') << offset << std::dec << ": "
             << text;
        os << line.str() << "\n";
    };

    auto invalid = [&]() -> std::optional<size_t> {
        print_offset("<truncated>");
        return std::nullopt;
    };

    switch (opcode) {
        case OPCODE_WRITE: {
            if (j + 2 > bytes.size()) {
                return invalid();
            }
            const uint8_t fd = bytes[j];
            const uint8_t slen = bytes[j + 1];
            j += 2;
            const std::string s = quoted_string(bytes, j, slen);
            j += std::min(static_cast<size_t>(slen), bytes.size() - j);
            std::ostringstream msg;
            msg << "write fd=" << static_cast<unsigned>(fd) << " \"" << s << "\"";
            print_offset(msg.str());
            break;
        }
        case OPCODE_EXEC: {
            if (j + 2 > bytes.size()) {
                return invalid();
            }
            const uint8_t reg = bytes[j];
            const uint8_t slen = bytes[j + 1];
            j += 2;
            const std::string s = quoted_string(bytes, j, slen);
            j += std::min(static_cast<size_t>(slen), bytes.size() - j);
            std::ostringstream msg;
            msg << "exec \"" << s << "\", " << reg_name(reg);
            print_offset(msg.str());
            break;
        }
        case OPCODE_NEWLINE:
            print_offset("newline");
            break;
        case OPCODE_PRINT: {
            uint8_t ch = 0;
            if (!blackbox::data::read_u8(bytes, j, ch)) {
                return invalid();
            }
            j += 1;
            std::ostringstream msg;
            msg << "print '" << static_cast<char>(ch) << "'";
            print_offset(msg.str());
            break;
        }
        case OPCODE_PUSHI: {
            int32_t v = 0;
            if (!blackbox::data::read_i32_le(bytes, j, v)) {
                return invalid();
            }
            j += 4;
            print_offset("pushi " + std::to_string(v));
            break;
        }
        case OPCODE_PUSH_REG:
        case OPCODE_POP:
        case OPCODE_PRINTREG:
        case OPCODE_EPRINTREG:
        case OPCODE_INC:
        case OPCODE_DEC:
        case OPCODE_PRINTSTR:
        case OPCODE_EPRINTSTR:
        case OPCODE_PRINTCHAR:
        case OPCODE_EPRINTCHAR:
        case OPCODE_READ:
        case OPCODE_READSTR:
        case OPCODE_READCHAR:
        case OPCODE_GETKEY:
        case OPCODE_NOT:
        case OPCODE_GETMODE:
        case OPCODE_GETFAULT:
        case OPCODE_GETARGC:
        case OPCODE_SLEEP_REG:
        case OPCODE_SYSCALL: {
            uint8_t r = 0;
            if (!blackbox::data::read_u8(bytes, j, r)) {
                return invalid();
            }
            j += 1;

            std::string name;
            switch (opcode) {
                case OPCODE_PUSH_REG:
                    name = "push";
                    break;
                case OPCODE_POP:
                    name = "pop";
                    break;
                case OPCODE_PRINTREG:
                    name = "printreg";
                    break;
                case OPCODE_EPRINTREG:
                    name = "eprintreg";
                    break;
                case OPCODE_INC:
                    name = "inc";
                    break;
                case OPCODE_DEC:
                    name = "dec";
                    break;
                case OPCODE_PRINTSTR:
                    name = "printstr";
                    break;
                case OPCODE_EPRINTSTR:
                    name = "eprintstr";
                    break;
                case OPCODE_PRINTCHAR:
                    name = "printchar";
                    break;
                case OPCODE_EPRINTCHAR:
                    name = "eprintchar";
                    break;
                case OPCODE_READ:
                    name = "read";
                    break;
                case OPCODE_READSTR:
                    name = "readstr";
                    break;
                case OPCODE_READCHAR:
                    name = "readchar";
                    break;
                case OPCODE_GETKEY:
                    name = "getkey";
                    break;
                case OPCODE_NOT:
                    name = "not";
                    break;
                case OPCODE_GETMODE:
                    name = "getmode";
                    break;
                case OPCODE_GETFAULT:
                    name = "getfault";
                    break;
                case OPCODE_GETARGC:
                    name = "getargc";
                    break;
                case OPCODE_SLEEP_REG:
                    name = "sleep";
                    break;
                case OPCODE_SYSCALL:
                    name = "syscall";
                    break;
                default:
                    name = "reg";
                    break;
            }

            if (opcode == OPCODE_SYSCALL) {
                print_offset(name + " " + std::to_string(static_cast<unsigned>(r)));
            } else {
                print_offset(name + " " + reg_name(r));
            }
            break;
        }
        case OPCODE_MOVI: {
            if (j + 5 > bytes.size()) {
                return invalid();
            }
            const uint8_t dst = bytes[j];
            int32_t imm = 0;
            if (!blackbox::data::read_i32_le(bytes, j + 1, imm)) {
                return invalid();
            }
            j += 5;
            print_offset("movi " + reg_name(dst) + ", " + std::to_string(imm));
            break;
        }
        case OPCODE_MOV_REG:
        case OPCODE_ADD:
        case OPCODE_SUB:
        case OPCODE_MUL:
        case OPCODE_DIV:
        case OPCODE_MOD:
        case OPCODE_XOR:
        case OPCODE_AND:
        case OPCODE_OR:
        case OPCODE_CMP:
        case OPCODE_LOAD_REG:
        case OPCODE_STORE_REG:
        case OPCODE_LOADVAR_REG:
        case OPCODE_STOREVAR_REG:
        case OPCODE_FREAD:
        case OPCODE_FWRITE_REG:
        case OPCODE_FSEEK_REG:
        case OPCODE_SHL:
        case OPCODE_SHR: {
            if (j + 2 > bytes.size()) {
                return invalid();
            }
            const uint8_t a = bytes[j];
            const uint8_t b = bytes[j + 1];
            j += 2;

            std::string op;
            switch (opcode) {
                case OPCODE_MOV_REG:
                    op = "mov";
                    break;
                case OPCODE_ADD:
                    op = "add";
                    break;
                case OPCODE_SUB:
                    op = "sub";
                    break;
                case OPCODE_MUL:
                    op = "mul";
                    break;
                case OPCODE_DIV:
                    op = "div";
                    break;
                case OPCODE_MOD:
                    op = "mod";
                    break;
                case OPCODE_XOR:
                    op = "xor";
                    break;
                case OPCODE_AND:
                    op = "and";
                    break;
                case OPCODE_OR:
                    op = "or";
                    break;
                case OPCODE_CMP:
                    op = "cmp";
                    break;
                case OPCODE_LOAD_REG:
                    op = "load";
                    break;
                case OPCODE_STORE_REG:
                    op = "store";
                    break;
                case OPCODE_LOADVAR_REG:
                    op = "loadvar";
                    break;
                case OPCODE_STOREVAR_REG:
                    op = "storevar";
                    break;
                case OPCODE_FREAD:
                    op = "fread";
                    break;
                case OPCODE_FWRITE_REG:
                    op = "fwrite";
                    break;
                case OPCODE_FSEEK_REG:
                    op = "fseek";
                    break;
                case OPCODE_SHL:
                    op = "shl";
                    break;
                case OPCODE_SHR:
                    op = "shr";
                    break;
                default:
                    op = "op";
                    break;
            }

            std::ostringstream msg;
            if (opcode == OPCODE_FREAD || opcode == OPCODE_FWRITE_REG ||
                opcode == OPCODE_FSEEK_REG) {
                msg << op << " " << file_name(a) << ", " << reg_name(b);
            } else {
                msg << op << " " << reg_name(a) << ", " << reg_name(b);
            }
            print_offset(msg.str());
            break;
        }
        case OPCODE_JMP:
        case OPCODE_JMPI:
        case OPCODE_JE:
        case OPCODE_JNE:
        case OPCODE_JL:
        case OPCODE_JGE:
        case OPCODE_JB:
        case OPCODE_JAE: {
            uint32_t addr = 0;
            if (!blackbox::data::read_u32_le(bytes, j, addr)) {
                return invalid();
            }
            j += 4;

            std::string op;
            switch (opcode) {
                case OPCODE_JMP:
                    op = "jmp";
                    break;
                case OPCODE_JMPI:
                    op = "jmpi";
                    break;
                case OPCODE_JE:
                    op = "je";
                    break;
                case OPCODE_JNE:
                    op = "jne";
                    break;
                case OPCODE_JL:
                    op = "jl";
                    break;
                case OPCODE_JGE:
                    op = "jge";
                    break;
                case OPCODE_JB:
                    op = "jb";
                    break;
                case OPCODE_JAE:
                    op = "jae";
                    break;
                default:
                    op = "j";
                    break;
            }

            print_offset(op + " 0x" + [&]() {
                std::ostringstream hex;
                hex << std::hex << std::setw(8) << std::setfill('0') << addr;
                return hex.str();
            }());
            break;
        }
        case OPCODE_ALLOC:
        case OPCODE_GROW:
        case OPCODE_RESIZE:
        case OPCODE_FREE:
        case OPCODE_SLEEP: {
            uint32_t value = 0;
            if (!blackbox::data::read_u32_le(bytes, j, value)) {
                return invalid();
            }
            j += 4;

            std::string op;
            switch (opcode) {
                case OPCODE_ALLOC:
                    op = "alloc";
                    break;
                case OPCODE_GROW:
                    op = "grow";
                    break;
                case OPCODE_RESIZE:
                    op = "resize";
                    break;
                case OPCODE_FREE:
                    op = "free";
                    break;
                case OPCODE_SLEEP:
                    op = "sleep";
                    break;
                default:
                    op = "u32";
                    break;
            }

            print_offset(op + " " + std::to_string(value));
            break;
        }
        case OPCODE_LOAD:
        case OPCODE_STORE:
        case OPCODE_LOADVAR:
        case OPCODE_STOREVAR:
        case OPCODE_LOADSTR:
        case OPCODE_LOADBYTE:
        case OPCODE_LOADWORD:
        case OPCODE_LOADDWORD:
        case OPCODE_LOADQWORD:
        case OPCODE_FWRITE_IMM:
        case OPCODE_FSEEK_IMM:
        case OPCODE_GETARG: {
            if (j + 5 > bytes.size()) {
                return invalid();
            }
            const uint8_t a = bytes[j];
            uint32_t b = 0;
            if (!blackbox::data::read_u32_le(bytes, j + 1, b)) {
                return invalid();
            }
            j += 5;

            std::string op;
            switch (opcode) {
                case OPCODE_LOAD:
                    op = "load";
                    break;
                case OPCODE_STORE:
                    op = "store";
                    break;
                case OPCODE_LOADVAR:
                    op = "loadvar";
                    break;
                case OPCODE_STOREVAR:
                    op = "storevar";
                    break;
                case OPCODE_LOADSTR:
                    op = "loadstr";
                    break;
                case OPCODE_LOADBYTE:
                    op = "loadbyte";
                    break;
                case OPCODE_LOADWORD:
                    op = "loadword";
                    break;
                case OPCODE_LOADDWORD:
                    op = "loaddword";
                    break;
                case OPCODE_LOADQWORD:
                    op = "loadqword";
                    break;
                case OPCODE_FWRITE_IMM:
                    op = "fwrite";
                    break;
                case OPCODE_FSEEK_IMM:
                    op = "fseek";
                    break;
                case OPCODE_GETARG:
                    op = "getarg";
                    break;
                default:
                    op = "op";
                    break;
            }

            std::ostringstream msg;
            if (opcode == OPCODE_FWRITE_IMM || opcode == OPCODE_FSEEK_IMM) {
                msg << op << " " << file_name(a) << ", " << static_cast<int32_t>(b);
            } else {
                msg << op << " " << reg_name(a) << ", " << b;
            }
            print_offset(msg.str());
            break;
        }
        case OPCODE_FOPEN: {
            if (j + 3 > bytes.size()) {
                return invalid();
            }
            const uint8_t mode = bytes[j];
            const uint8_t fd = bytes[j + 1];
            const uint8_t name_len = bytes[j + 2];
            j += 3;

            const std::string name = quoted_string(bytes, j, name_len);
            j += std::min(static_cast<size_t>(name_len), bytes.size() - j);

            std::ostringstream msg;
            msg << "fopen mode=" << static_cast<unsigned>(mode)
                << " fd=" << static_cast<unsigned>(fd) << " \"" << name << "\"";
            print_offset(msg.str());
            break;
        }
        case OPCODE_FCLOSE: {
            uint8_t fd = 0;
            if (!blackbox::data::read_u8(bytes, j, fd)) {
                return invalid();
            }
            j += 1;
            print_offset("fclose " + file_name(fd));
            break;
        }
        case OPCODE_RAND: {
            if (j + 17 > bytes.size()) {
                return invalid();
            }
            const uint8_t reg = bytes[j];
            uint64_t min_value = 0;
            uint64_t max_value = 0;
            if (!blackbox::data::read_u64_le(bytes, j + 1, min_value) ||
                !blackbox::data::read_u64_le(bytes, j + 9, max_value)) {
                return invalid();
            }
            j += 17;

            std::ostringstream msg;
            msg << "rand " << reg_name(reg) << " min=" << static_cast<int64_t>(min_value)
                << " max=" << static_cast<int64_t>(max_value);
            print_offset(msg.str());
            break;
        }
        case OPCODE_CALL: {
            uint32_t addr = 0;
            uint32_t frame = 0;
            if (!blackbox::data::read_u32_le(bytes, j, addr) ||
                !blackbox::data::read_u32_le(bytes, j + 4, frame)) {
                return invalid();
            }
            j += 8;
            std::ostringstream msg;
            msg << "call 0x" << std::hex << std::setw(8) << std::setfill('0') << addr << std::dec
                << " frame=" << frame;
            print_offset(msg.str());
            break;
        }
        case OPCODE_RET:
            print_offset("ret");
            break;
        case OPCODE_PRINT_STACKSIZE:
            print_offset("print_stacksize");
            break;
        case OPCODE_HALT:
            if (j < bytes.size()) {
                const uint8_t code_byte = bytes[j++];
                print_offset("halt " + std::to_string(static_cast<unsigned>(code_byte)));
            } else {
                print_offset("halt");
            }
            break;
        case OPCODE_BREAK:
            print_offset("break");
            break;
        case OPCODE_CONTINUE:
            print_offset("continue");
            break;
        case OPCODE_CLRSCR:
            print_offset("clrscr");
            break;
        case OPCODE_REGSYSCALL:
        case OPCODE_REGFAULT: {
            if (j + 5 > bytes.size()) {
                return invalid();
            }
            const uint8_t id = bytes[j];
            uint32_t addr = 0;
            if (!blackbox::data::read_u32_le(bytes, j + 1, addr)) {
                return invalid();
            }
            j += 5;

            std::string op = opcode == OPCODE_REGSYSCALL ? "regsyscall" : "regfault";
            std::ostringstream msg;
            msg << op << " " << static_cast<unsigned>(id) << ", 0x" << std::hex << std::setw(8)
                << std::setfill('0') << addr;
            print_offset(msg.str());
            break;
        }
        case OPCODE_SYSRET:
            print_offset("sysret");
            break;
        case OPCODE_DROPPRIV:
            print_offset("droppriv");
            break;
        case OPCODE_SETPERM: {
            if (j + 12 > bytes.size()) {
                return invalid();
            }
            uint32_t start = 0;
            uint32_t count = 0;
            if (!blackbox::data::read_u32_le(bytes, j, start) ||
                !blackbox::data::read_u32_le(bytes, j + 4, count)) {
                return invalid();
            }
            const uint8_t priv_r = bytes[j + 8];
            const uint8_t priv_w = bytes[j + 9];
            const uint8_t prot_r = bytes[j + 10];
            const uint8_t prot_w = bytes[j + 11];
            j += 12;

            std::ostringstream msg;
            msg << "setperm start=" << start << " count=" << count << " priv("
                << static_cast<unsigned>(priv_r) << static_cast<unsigned>(priv_w) << ") prot("
                << static_cast<unsigned>(prot_r) << static_cast<unsigned>(prot_w) << ")";
            print_offset(msg.str());
            break;
        }
        case OPCODE_DUMPREGS:
            print_offset("dumpregs");
            break;
        case OPCODE_FAULTRET:
            print_offset("faultret");
            break;
        case OPCODE_SHLI:
        case OPCODE_SHRI: {
            if (j + 9 > bytes.size()) {
                return invalid();
            }
            const uint8_t reg = bytes[j];
            uint64_t amount = 0;
            if (!blackbox::data::read_u64_le(bytes, j + 1, amount)) {
                return invalid();
            }
            j += 9;

            std::string op = opcode == OPCODE_SHLI ? "shli" : "shri";
            print_offset(op + " " + reg_name(reg) + ", " + std::to_string(amount));
            break;
        }
        case OPCODE_GETENV: {
            if (j + 2 > bytes.size()) {
                return invalid();
            }
            const uint8_t reg = bytes[j];
            const uint8_t len = bytes[j + 1];
            j += 2;
            const std::string name = quoted_string(bytes, j, len);
            j += std::min(static_cast<size_t>(len), bytes.size() - j);

            print_offset("getenv " + reg_name(reg) + ", \"" + name + "\"");
            break;
        }
        default: {
            std::ostringstream msg;
            msg << "unknown opcode 0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(opcode) << std::dec;
            print_offset(msg.str());
            break;
        }
    }

    if (j > bytes.size()) {
        return std::nullopt;
    }
    return j;
}

bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);

    if (size < 0) {
        return false;
    }

    out.resize(static_cast<size_t>(size));
    if (size == 0) {
        return true;
    }

    input.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<std::streamsize>(input.gcount()) == size;
}

bool parse_options(int argc, char** argv, Options& opts) {
    opts = Options{};

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--dump") {
            opts.dump = true;
        } else if (arg == "--decomp") {
            opts.decomp = true;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::println(stderr, "error: missing output file after {}", arg);
                return false;
            }
            opts.output_path = std::string(argv[++i]);
        } else {
            opts.input_path = arg;
        }
    }

    return true;
}

void run_decomp(const ParsedImage& image, std::ostream& os) {
    ScanContext scan_ctx;
    size_t i = 0;
    while (i < image.code.size()) {
        const std::optional<size_t> next = scan_one(image.code, i, image.code_start, scan_ctx);
        if (!next.has_value()) {
            break;
        }
        i = *next;
    }

    const std::vector<DataEntry> data_entries =
        reconstruct_data_entries(image.data_table, scan_ctx.data_refs);

    os << "%asm\n";
    emit_data_section(os, image.data_table, data_entries);
    os << "%main\n";

    i = 0;
    while (i < image.code.size()) {
        const std::optional<size_t> next =
            emit_instruction_decomp(os, image.code, i, image.code_start, scan_ctx, data_entries);
        if (!next.has_value()) {
            break;
        }
        i = *next;
    }
}

void run_dump(const ParsedImage& image, std::ostream& os) {
    if (image.has_header) {
        os << "Header: MAGIC=bcx data_count=" << static_cast<unsigned>(image.data_count)
           << " data_table_size=" << image.data_table.size() << "\n";
    }

    std::vector<uint8_t> code_with_offset;
    code_with_offset.resize(image.code_start + image.code.size());
    std::copy(image.code.begin(), image.code.end(),
              code_with_offset.begin() + static_cast<std::ptrdiff_t>(image.code_start));

    size_t i = image.code_start;
    while (i < code_with_offset.size()) {
        const std::optional<size_t> next = emit_instruction_dump(os, code_with_offset, i);
        if (!next.has_value()) {
            break;
        }
        i = *next;
    }
}

void run_decomp_stdout(const ParsedImage& image) {
    std::ostringstream out;
    run_decomp(image, out);
    std::println("{}", out.str());
}

void run_dump_stdout(const ParsedImage& image) {
    std::ostringstream out;
    run_dump(image, out);
    std::println("{}", out.str());
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return 1;
    }

    if (!options.dump && !options.decomp) {
        std::println(stderr, "Usage: {} [--dump|--decomp] [-o file] [input]",
                     argc > 0 ? argv[0] : "bbxd");
        return 1;
    }

    std::vector<uint8_t> raw;
    if (!read_file_bytes(options.input_path, raw)) {
        std::println(stderr, "error: failed to read {}", options.input_path);
        return 1;
    }

    ParsedImage image;
    if (!parse_image(raw, image)) {
        std::println(stderr, "file truncated: invalid header/data table sizing");
        return 1;
    }

    if (options.decomp) {
        if (options.output_path.has_value()) {
            std::ofstream out(*options.output_path, std::ios::binary);
            if (!out) {
                std::println(stderr, "error: failed to create {}", *options.output_path);
                return 1;
            }
            run_decomp(image, out);
            return 0;
        }
        run_decomp_stdout(image);
        return 0;
    }

    run_dump_stdout(image);
    return 0;
}
