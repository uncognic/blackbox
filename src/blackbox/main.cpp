//
// Created by User on 2026-04-18.
//
#include "debugger.hpp"
#include "program.hpp"
#include "vm.hpp"
#include <filesystem>
#include <print>
#include <string_view>
namespace {
void print_usage() {
    std::println("Usage: bbx [--debug|-d] [--step|-s] <program.bcx>");
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::filesystem::path prog_path;
    bool debug = false;
    bool step_mode = false;

    for (int i = 1; i < argc; i++) {
        std::string_view arg(argv[i]);
        if (arg == "--debug" || arg == "-d") {
            debug = true;
        } else if (arg == "--step" || arg == "-s") {
            debug = true;
            step_mode = true;
        } else if (prog_path.empty()) {
            prog_path = arg;
        } else {
            std::println(stderr, "Unexpected argument: {}", arg);
            print_usage();
            return 1;
        }
    }

    if (prog_path.empty()) {
        print_usage();
        return 1;
    }

    auto result = Program::load(prog_path);
    if (!result) {
        std::println(stderr, "Error loading '{}': {}", prog_path.string(), result.error());
        return 1;
    }

    VM vm(std::move(*result), argc, argv);

    if (debug) {
        Debugger::Mode mode = step_mode ? Debugger::Mode::Step : Debugger::Mode::Breakpoint;
        Debugger dbg(vm, mode);
        return dbg.run();
    }

    return vm.run();
}
