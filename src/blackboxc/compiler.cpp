#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../define.h"
#include "tools.h"
#include "asm.h"
#include "basic.h"
#include "../data.h"

static std::string trim_copy(const std::string &text)
{
    size_t start = 0;
    while (start < text.size() && std::isspace((unsigned char)text[start]))
        start++;

    size_t end = text.size();
    while (end > start && std::isspace((unsigned char)text[end - 1]))
        end--;

    return text.substr(start, end - start);
}

static bool is_help_argument(const std::string &argument)
{
    return argument == "-h" || argument == "--help";
}

static bool is_debug_argument(const std::string &argument)
{
    return argument == "-d" || argument == "--debug";
}

static bool is_asm_argument(const std::string &argument)
{
    return argument == "-a" || argument == "--asm";
}

int main(int argc, char *argv[])
{
    if (argc > 1 && is_help_argument(argv[1]))
    {
        std::printf("Usage: %s [-d, --debug] [-h, --help] [-a, --asm] input.bbx/.bbs <output.bcx>\n", argv[0]);
        return 1;
    }

    std::string input_file;
    std::string output_file;
    bool debug = false;
    bool assembly = false;

    for (int i = 1; i < argc; i++)
    {
        std::string argument = argv[i];
        if (is_debug_argument(argument))
            debug = true;
        else if (is_asm_argument(argument))
            assembly = true;
        else if (input_file.empty())
            input_file = argument;
        else if (output_file.empty())
            output_file = argument;
        else
        {
            std::fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (input_file.empty())
    {
        std::fprintf(stderr, "Usage: %s [-d, --debug] [-h, --help] [-a, --asm] input.bbx/.bbs <output.bcx>\n", argv[0]);
        return 1;
    }

    std::string asm_output;

    if (assembly)
    {
        size_t separator = input_file.find_last_of("\\/");
        std::string filename = (separator == std::string::npos) ? input_file : input_file.substr(separator + 1);
        asm_output = filename + ".bbx";
        output_file = asm_output;
    }
    else if (output_file.empty())
    {
        std::printf("Output file not specified, defaulting to 'out.bcx'\n");
        output_file = "out.bcx";
    }

    std::ifstream input_stream(input_file);
    if (!input_stream)
    {
        std::perror("fopen input");
        return 1;
    }

    std::string line;
    bool is_asm = false;
    while (std::getline(input_stream, line))
    {
        std::string trimmed = trim_copy(line);
        size_t comment = trimmed.find(';');
        if (comment != std::string::npos)
            trimmed.erase(comment);
        trimmed = trim_copy(trimmed);
        if (trimmed.empty())
            continue;
        is_asm = equals_ci(trimmed.c_str(), "%asm") != 0;
        break;
    }

    int result;
    if (is_asm)
    {
        if (debug)
        {
            std::printf("Debug mode ON\n");
            std::printf("[DEBUG] Input file:  %s\n", input_file.c_str());
            std::printf("[DEBUG] Output file: %s\n", output_file.c_str());
            std::printf("[DEBUG] Pathway: assembly\n");
        }
        result = assemble_file(input_file.c_str(), output_file.c_str(), debug ? 1 : 0);
        if (result == 0)
            std::printf("Assembly successful.\n");
    }
    else
    {
        if (debug)
        {
            std::printf("Debug mode ON\n");
            std::printf("[DEBUG] Input file:  %s\n", input_file.c_str());
            std::printf("[DEBUG] Output file: %s\n", output_file.c_str());
            std::printf("[DEBUG] Pathway: basic\n");
        }
        result = preprocess_basic(input_file.c_str(), output_file.c_str(), debug ? 1 : 0);

        if (result != 0)
            return result;

        std::printf("BASIC preprocessing successful.\n");

        if (assembly)
            return result;

        result = assemble_file(output_file.c_str(), output_file.c_str(), debug ? 1 : 0);
        if (result == 0)
            std::printf("Assembly successful.\n");
    }

    return result;
}