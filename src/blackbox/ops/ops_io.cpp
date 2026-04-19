//
// Created by User on 2026-04-18.
//

#include "ops_io.hpp"
#include "../vm.hpp"
#include <format>
#include <print>
#include <iostream>

void VM::op_print() {
    uint8_t val = fetch_u8();
    std::print("{}", static_cast<char>(val));
}

void VM::op_newline() {
    std::print("\n");
}

void VM::op_printreg() {
    size_t reg = fetch_reg();
    std::print("{}", regs[reg]);
}

void VM::op_eprintreg() {
    size_t reg = fetch_reg();
    std::print(stderr, "{}", regs[reg]);
}

void VM::op_printchar() {
    size_t reg = fetch_reg();
    std::print("{}", static_cast<char>(regs[reg]));
}

void VM::op_eprintchar() {
    size_t reg = fetch_reg();
    std::print(stderr, "{}", static_cast<char>(regs[reg]));
}

void VM::op_loadstr() {
    size_t reg = fetch_reg();
    uint32_t index = fetch_u32();
    if (!prog.strings.valid(index)) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("LOADSTR invalid string index {} at pc={}", index, pc));
    }
    regs[reg] = static_cast<int64_t>(index);
}

void VM::op_printstr() {
    size_t reg = fetch_reg();
    uint32_t index = static_cast<uint32_t>(regs[reg]);
    if (!prog.strings.valid(index)) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("PRINTSTR invalid string index {} at pc={}", index, pc));
    }
    std::print("{}", prog.strings.get(index));
}

void VM::op_eprintstr() {
    size_t reg = fetch_reg();
    uint32_t index = static_cast<uint32_t>(regs[reg]);
    if (!prog.strings.valid(index)) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("EPRINTSTR invalid string index {} at pc={}", index, pc));
    }
    std::print(stderr, "{}", prog.strings.get(index));
}


void VM::op_write() {
    uint8_t fd = fetch_u8();
    uint8_t len = fetch_u8();

    if (fd != 1 && fd != 2) {
        hard_fault(FaultType::OutOfBounds, std::format("WRITE invalid fd {} at pc={}", fd, pc));
    }
    if (pc + len > prog.code.size()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("WRITE string past end of code at pc={}", pc));
    }

    std::string_view sv(reinterpret_cast<const char*>(prog.code.data() + pc), len);
    if (fd == 1) {
        std::print("{}", sv);
    } else {
        std::print(stderr, "{}", sv);
    }
    pc += len;
}
// TODO: make better
void VM::op_read() {
    size_t reg = fetch_reg();
    long long v = 0;
    if (std::scanf("%lld", &v) != 1) {
        v = 0;
    }

    int c;
    while ((c = std::getchar()) != EOF && c != '\n') {
    }
    regs[reg] = static_cast<int64_t>(v);
}

void VM::op_readstr() {
    size_t reg = fetch_reg();
    std::string line;
    std::getline(std::cin, line);
    uint32_t handle = prog.strings.intern(line);
    regs[reg] = static_cast<int64_t>(handle);
}

void VM::op_readchar() {
    size_t reg = fetch_reg();
    int c;
    while ((c = std::getchar()) != EOF && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
    }
    if (c == EOF) {
        regs[reg] = 0;
        return;
    }
    regs[reg] = static_cast<int64_t>(static_cast<unsigned char>(c));
    int ch;
    while ((ch = std::getchar()) != EOF && ch != '\n') {
    }
}

void VM::op_fopen() {
    require_privileged("FOPEN");

    uint8_t mode_byte = fetch_u8();
    uint8_t fd = fetch_u8();
    uint8_t fname_len = fetch_u8();

    if (fd >= FILE_DESCRIPTORS) {
        hard_fault(FaultType::OutOfBounds, std::format("FOPEN invalid fd {} at pc={}", fd, pc));
    }
    if (pc + fname_len > prog.code.size()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("FOPEN filename past end of code at pc={}", pc));
    }

    std::string fname(reinterpret_cast<const char*>(prog.code.data() + pc), fname_len);
    pc += fname_len;

    // close existing
    fds[fd].kind = FD::Kind::Closed;
    fds[fd].file.reset();

    if (fname == "/dev/stdout") {
        fds[fd].kind = FD::Kind::StdOut;
        return;
    }
    if (fname == "/dev/stderr") {
        fds[fd].kind = FD::Kind::StdErr;
        return;
    }
    if (fname == "/dev/stdin") {
        fds[fd].kind = FD::Kind::StdIn;
        return;
    }

    std::ios::openmode mode{};
    switch (mode_byte) {
        case 0:
            mode = std::ios::in;
            break;
        case 1:
            mode = std::ios::out | std::ios::trunc;
            break;
        case 2:
            mode = std::ios::out | std::ios::app;
            break;
        default:
            hard_fault(FaultType::OutOfBounds,
                       std::format("FOPEN invalid mode {} at pc={}", mode_byte, pc));
    }

    auto file = std::make_unique<std::fstream>(fname, mode);
    if (!file->is_open()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("FOPEN failed to open '{}' at pc={}", fname, pc));
    }
    fds[fd].kind = FD::Kind::File;
    fds[fd].file = std::move(file);
}

void VM::op_fclose() {
    require_privileged("FCLOSE");
    uint8_t fd = fetch_u8();
    if (fd >= FILE_DESCRIPTORS) {
        hard_fault(FaultType::OutOfBounds, std::format("FCLOSE invalid fd {} at pc={}", fd, pc));
    }
    fds[fd].kind = FD::Kind::Closed;
    fds[fd].file.reset();
}

void VM::op_fread() {
    uint8_t fd = fetch_u8();
    size_t reg = fetch_reg();

    if (fd >= FILE_DESCRIPTORS) {
        hard_fault(FaultType::OutOfBounds, std::format("FREAD invalid fd {} at pc={}", fd, pc));
    }
    std::istream* in = fds[fd].reader();
    if (!in) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("FREAD fd {} not open for reading at pc={}", fd, pc));
    }
    int c = in->get();
    regs[reg] = (c == EOF) ? -1 : static_cast<int64_t>(c);
}

void VM::op_fwrite_reg() {
    uint8_t fd = fetch_u8();
    size_t reg = fetch_reg();

    if (fd >= FILE_DESCRIPTORS) {
        hard_fault(FaultType::OutOfBounds, std::format("FWRITE invalid fd {} at pc={}", fd, pc));
    }
    std::ostream* out = fds[fd].writer();
    if (!out) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("FWRITE fd {} not open for writing at pc={}", fd, pc));
    }
    out->put(static_cast<char>(regs[reg]));
    out->flush();
}

void VM::op_fwrite_imm() {
    uint8_t fd = fetch_u8();
    int32_t val = fetch_i32();

    if (fd >= FILE_DESCRIPTORS) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("FWRITE_IMM invalid fd {} at pc={}", fd, pc));
    }
    std::ostream* out = fds[fd].writer();
    if (!out) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("FWRITE_IMM fd {} not open for writing at pc={}", fd, pc));
    }
    out->put(static_cast<char>(val));
    out->flush();
}

void VM::op_fseek_reg() {
    uint8_t fd = fetch_u8();
    size_t reg = fetch_reg();

    if (fd >= FILE_DESCRIPTORS) {
        hard_fault(FaultType::OutOfBounds, std::format("FSEEK invalid fd {} at pc={}", fd, pc));
    }
    auto pos = static_cast<std::streamoff>(regs[reg]);
    if (std::istream* in = fds[fd].reader()) {
        in->clear();
        in->seekg(pos, std::ios::beg);
    }
    if (std::ostream* out = fds[fd].writer()) {
        out->clear();
        out->seekp(pos, std::ios::beg);
    }
}

void VM::op_fseek_imm() {
    uint8_t fd = fetch_u8();
    int32_t offset = fetch_i32();

    if (fd >= FILE_DESCRIPTORS) {
        hard_fault(FaultType::OutOfBounds, std::format("FSEEK_IMM invalid fd {} at pc={}", fd, pc));
    }
    auto pos = static_cast<std::streamoff>(offset);
    if (std::istream* in = fds[fd].reader()) {
        in->clear();
        in->seekg(pos, std::ios::beg);
    }
    if (std::ostream* out = fds[fd].writer()) {
        out->clear();
        out->seekp(pos, std::ios::beg);
    }
}
