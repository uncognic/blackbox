//
// Created by User on 2026-04-18.
//

#ifndef BLACKBOX_DEBUGGER_HPP
#define BLACKBOX_DEBUGGER_HPP

#include "vm.hpp"
#include <string>

class Debugger {
public:
    enum class Mode {
        Step,
        Breakpoint,
    };

    explicit Debugger(VM& vm, Mode mode);
    int run();

private:
    VM& vm;
    Mode mode;
    bool instructions_shown = false;

    void print_instructions();
    void handle_command(std::string_view cmd);
    void print_regs(int from, int to);
    void print_reg(size_t reg);
    void print_stack(int count);
    void print_stack_range(int from, int to);
};
#endif //BLACKBOX_DEBUGGER_HPP
