#include "assembler.hpp"
#include "basic/basic.hpp"
#include "utils/string_utils.hpp"
#include <filesystem>
#include <fstream>
#include <print>
#include <string_view>

namespace {
void print_usage(std::string_view prog) {
    std::println("Usage: {} [-d|--debug] [-h|--help] [-a|--asm] <input> [output.bcx]", prog);
}
bool is_flag(std::string_view arg, std::string_view s, std::string_view l) {
    return arg == s || arg == l;
}
} // namespace

int main(int argc, char** argv) {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    bool debug = false;
    bool asm_only = false;

    for (int i = 1; i < argc; i++) {
        std::string_view arg(argv[i]);
        if (is_flag(arg, "-h", "--help")) {
            print_usage(argv[0]);
            return 0;
        } else if (is_flag(arg, "-d", "--debug")) {
            debug = true;
        } else if (is_flag(arg, "-a", "--asm")) {
            asm_only = true;
        } else if (input_path.empty()) {
            input_path = arg;
        } else if (output_path.empty()) {
            output_path = arg;
        } else {
            std::println(stderr, "Unexpected argument: {}", arg);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (input_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    std::string ext = input_path.extension().string();

    if (output_path.empty()) {
        if (asm_only) {
            output_path = input_path.stem();
            output_path += ".bcx";
        } else {
            output_path = "out.bcx";
            std::println("Output file not specified, defaulting to 'out.bcx'");
        }
    }

    bool is_basic = (ext == ".bbs");
    bool is_asm = (ext == ".bbx") || asm_only;

    // if is neither
    if (!is_basic && !is_asm) {
        std::ifstream f(input_path);
        std::string line;
        while (std::getline(f, line)) {
            size_t c = line.find(';');
            if (c != std::string::npos) {
                line.erase(c);
            }
            size_t s = line.find_first_not_of(" \t\r\n");
            if (s == std::string::npos) {
                continue;
            }
            line = line.substr(s);
            is_asm = blackbox::tools::equals_ci(line.c_str(), "%asm");
            break;
        }
        if (!is_asm) {
            is_basic = true;
        }
    }

    if (debug) {
        std::println("[DEBUG] input:  {}", input_path.string());
        std::println("[DEBUG] output: {}", output_path.string());
        std::println("[DEBUG] mode:   {}", is_asm ? "assembly" : "basic");
    }

    if (is_basic) {
        std::filesystem::path intermediate = output_path;
        intermediate.replace_extension(".bbx");

        if (auto err = preprocess_basic(input_path, intermediate, debug)) {
            std::println(stderr, "BASIC error: {}", *err);
            return 1;
        }

        std::println("BASIC preprocessing successful.");

        if (asm_only) {
            return 0;
        }

        auto result = Assembler::assemble(intermediate, output_path, debug);
        if (!result) {
            std::println(stderr, "Assembly error: {}", result.error());
            return 1;
        }
        std::println("Assembly successful.");
        return 0;
    }

    auto result = Assembler::assemble(input_path, output_path, debug);
    if (!result) {
        std::println(stderr, "Assembly error: {}", result.error());
        return 1;
    }
    std::println("Assembly successful.");
    return 0;
}