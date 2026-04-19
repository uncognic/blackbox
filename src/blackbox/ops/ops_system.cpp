//
// Created by User on 2026-04-18.
//

#include "ops_system.hpp"
#include "../vm.hpp"
#include <cstdlib>
#include <format>
#include <print>
#include "../../utils/random_utils.hpp"

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

void VM::op_exec() {
    require_privileged("EXEC");

    size_t dst = fetch_reg();
    uint8_t len = fetch_u8();

    if (pc + len > prog.code.size()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("EXEC command past end of code at pc={}", pc));
    }

    std::string cmd(reinterpret_cast<const char*>(prog.code.data() + pc), len);
    pc += len;

    regs[dst] = static_cast<int64_t>(std::system(cmd.c_str()));
}

static void sleep_ms(uint64_t ms) {
#ifdef _WIN32
    Sleep(static_cast<DWORD>(ms));
#else
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = static_cast<long>((ms % 1000) * 1000000L);
    nanosleep(&req, nullptr);
#endif
}

void VM::op_sleep() {
    uint32_t ms = fetch_u32();
    sleep_ms(static_cast<uint64_t>(ms));
}

void VM::op_sleep_reg() {
    size_t reg = fetch_reg();
    int64_t raw = regs[reg];
    sleep_ms(raw < 0 ? 0 : static_cast<uint64_t>(raw));
}

void VM::op_rand() {
    size_t reg = fetch_reg();
    int64_t min = fetch_i64();
    int64_t max = fetch_i64();

    if (min > max) {
        std::swap(min, max);
    }

    uint64_t r = blackbox::tools::get_true_random();

    uint64_t range = static_cast<uint64_t>(max - min) + 1;
    if (range == 0) {
        regs[reg] = static_cast<int64_t>(r);
    } else {
        regs[reg] = min + static_cast<int64_t>(r % range);
    }
}

void VM::op_getkey() {
    size_t reg = fetch_reg();

#ifdef _WIN32
    if (_kbhit()) {
        regs[reg] = static_cast<int64_t>(_getch());
    } else {
        regs[reg] = -1;
    }
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    int ch = std::getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    if (ch == EOF) {
        clearerr(stdin);
        regs[reg] = -1;
    } else {
        regs[reg] = static_cast<int64_t>(ch);
    }
#endif
}

void VM::op_clrscr() {
    std::print("\x1b[2J\x1b[H");
}

void VM::op_getargc() {
    size_t reg = fetch_reg();
    regs[reg] = static_cast<int64_t>(host_argc);
}

void VM::op_getarg() {
    size_t reg = fetch_reg();
    uint32_t idx = fetch_u32();

    if (static_cast<int>(idx) >= host_argc) {
        hard_fault(
            FaultType::OutOfBounds,
            std::format("GETARG index {} out of bounds (argc={}) at pc={}", idx, host_argc, pc));
    }

    std::string_view arg(host_argv[idx]);
    uint32_t handle = prog.strings.intern(arg);
    regs[reg] = static_cast<int64_t>(handle);
}

void VM::op_getenv() {
    size_t reg = fetch_reg();
    uint8_t envlen = fetch_u8();

    if (pc + envlen > prog.code.size()) {
        hard_fault(FaultType::OutOfBounds,
                   std::format("GETENV name past end of code at pc={}", pc));
    }

    std::string_view name(reinterpret_cast<const char*>(prog.code.data() + pc), envlen);
    pc += envlen;

    const char* val = std::getenv(std::string(name).c_str());
    if (!val) {
        raise_fault(FaultType::EnvVarNotFound,
                    std::format("GETENV '{}' not found at pc={}", name, pc));
        return;
    }

    uint32_t handle = prog.strings.intern(std::string_view(val));
    regs[reg] = static_cast<int64_t>(handle);
}