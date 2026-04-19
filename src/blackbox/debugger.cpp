//
// Created by User on 2026-04-18.
//

#include "debugger.hpp"
#include <format>
#include <print>
#include <string>
#include <charconv>
#include <iostream>

namespace {
bool parse_int(std::string_view s, int& out) {
    auto result = std::from_chars(s.data(), s.data() + s.size(), out);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}
std::string_view trim_left(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    return s;
}
}

Debugger::Debugger(VM& vm, Mode mode) : vm(vm) , mode(mode){}

void Debugger::print_instructions() {
    if (instructions_shown) return;
    std::println("Debugger commands:");
    std::println("  Enter   - step one instruction");
    std::println("  c       - continue (disable debugger)");
    std::println("  q       - quit");
    std::println("  r       - show top 8 registers");
    std::println("  rN      - show register N (e.g. r3)");
    std::println("  rM-N    - show registers from M to N");
    std::println("  s       - show top 8 stack entries");
    std::println("  sN      - show top N stack entries (e.g. s10)");
    std::println("  sM-N    - show stack entries from M to N");
    instructions_shown = true;
}

void Debugger::print_regs(int from, int to) {
    for (int i = from; i <= to; i++) {
        std::println("r{} = {}", i, vm.get_reg(i));
    }
}

void Debugger::print_stack(int count) {
    size_t depth = vm.get_call_depth();
    if (count <= 0) count = 8;
    std::println("call depth: {}", depth);
}

void Debugger::handle_command(std::string_view raw) {
    std::string_view cmd = trim_left(raw);

    // strip trailing newline
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) {
        cmd.remove_suffix(1);
    }

    if (cmd.empty()) return; // step — caller handles

    if (cmd[0] == 'q') {
        std::exit(0);
    }

    if (cmd[0] == 'r') {
        std::string_view rest = trim_left(cmd.substr(1));
        if (rest.empty()) {
            print_regs(0,8);
            return;
        }

        size_t dash = rest.find('-');
        if (dash != std::string_view::npos) {
            int a = 0, b = 0;
            parse_int(rest.substr(0, dash), a);
            parse_int(rest.substr(dash + 1), b);
            print_regs(a, b);
            return;
        }

        int n = 8;
        parse_int(rest, n);
        print_reg(n);
        return;
    }

    if (cmd[0] == 's') {
        std::string_view rest = trim_left(cmd.substr(1));
        if (rest.empty()) {
            print_stack(8);
            return;
        }
        size_t dash = rest.find('-');
        if (dash != std::string_view::npos) {
            int a = 0, b = 0;
            parse_int(rest.substr(0, dash), a);
            parse_int(rest.substr(dash + 1), b);
            print_stack_range(a, b);
            return;
        }
        int n = 8;
        parse_int(rest, n);
        print_stack(n);
        return;
    }
}

int Debugger::run() {
    while (!vm.is_halted()) {
        vm.step();

        if (vm.hit_breakpoint()) {
            vm.clear_hit_breakpoint();
            std::println("[BREAK] pc={}", vm.get_pc());
            print_instructions();

            bool resume = false;
            while (!resume) {
                std::print("> ");
                std::string cmd;
                std::getline(std::cin, cmd);

                if (cmd == "c") {
                    resume = true;
                } else if (cmd == "q") {
                    std::exit(0);
                } else {
                    handle_command(cmd);
                }
            }
        }

        if (mode == Mode::Step) {
            std::println("[pc={}]", vm.get_pc());
            print_instructions();

            bool advance = false;
            while (!advance) {
                std::print("> ");
                std::string cmd;
                std::getline(std::cin, cmd);

                if (cmd.empty()) {
                    advance = true;
                } else if (cmd == "c") {
                    mode = Mode::Breakpoint;
                    advance = true;
                } else if (cmd == "q") {
                    std::exit(0);
                } else {
                    handle_command(cmd);
                }
            }
        }
    }
    return vm.get_exit_code();
}