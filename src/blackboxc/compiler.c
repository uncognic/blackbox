#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../define.h"
#include "tools.h"
#include "asm.h"
#include "basic.h"
#include "../data.h"

int main(int argc, char *argv[])
{
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        fprintf(stdout, "Usage: %s [-d, --debug] [-h, --help] [-a, --asm] input.bbx/.bbs <output.bcx>\n", argv[0]);
        return 1;
    }

    char *input_file = NULL;
    char *output_file = NULL;
    uint8_t debug = 0;
    uint8_t assembly = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0)
            debug = 1;
        else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--asm") == 0)
            assembly = 1;
        else if (!input_file)
            input_file = argv[i];
        else if (!output_file)
            output_file = argv[i];
        else
        {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!input_file)
    {
        fprintf(stderr, "Usage: %s [-d, --debug] [-h, --help] [-a, --asm] input.bbx/.bbs <output.bcx>\n", argv[0]);
        return 1;
    }

    char *asm_output = NULL;

    if (assembly)
    {
        char *filename = strrchr(input_file, '\\');
        if (!filename)
            filename = strrchr(input_file, '/');
        if (filename)
            filename++;
        else
            filename = input_file;

        asm_output = malloc(strlen(filename) + 5);
        if (!asm_output)
        {
            perror("malloc");
            return 1;
        }
        sprintf(asm_output, "%s.bbx", filename);
        output_file = asm_output;
    }
    else if (!output_file)
    {
        printf("Output file not specified, defaulting to 'out.bcx'\n");
        output_file = "out.bcx";
    }

    FILE *in = fopen(input_file, "r");
    if (!in)
    {
        perror("fopen input");
        free(asm_output);
        return 1;
    }

    char peek[256];
    int is_asm = 0;
    while (fgets(peek, sizeof(peek), in))
    {
        char *s = trim(peek);
        char *comment = strchr(s, ';');
        if (comment)
            *comment = '\0';
        s = trim(s);
        if (*s == '\0')
            continue;
        is_asm = equals_ci(s, "%asm");
        break;
    }
    fclose(in);

    int result;
    if (is_asm)
    {
        if (debug)
        {
            printf("Debug mode ON\n");
            printf("[DEBUG] Input file:  %s\n", input_file);
            printf("[DEBUG] Output file: %s\n", output_file);
            printf("[DEBUG] Pathway: assembly\n");
        }
        result = assemble_file(input_file, output_file, debug);
        if (result == 0)
            printf("Assembly successful.\n");
    }
    else
    {
        if (debug)
        {
            printf("Debug mode ON\n");
            printf("[DEBUG] Input file:  %s\n", input_file);
            printf("[DEBUG] Output file: %s\n", output_file);
            printf("[DEBUG] Pathway: basic\n");
        }
        result = preprocess_basic(input_file, output_file, debug);

        if (result == 0)
            printf("BASIC preprocessing successful.\n");

        if (assembly)
        {
            free(asm_output);
            return result;
        }

        result = assemble_file(output_file, output_file, debug);
        if (result == 0)
            printf("Assembly successful.\n");
    }

    free(asm_output);
    return result;
}