#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../define.h"
#include "tools.h"
#include "asm.h" 

int main(int argc, char *argv[])
{
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
    {
        fprintf(stdout, "Usage: %s [-d, --debug] [-h, --help] input.bbx output.bcx\n", argv[0]);
        return 1;
    }
    char *input_file = NULL;
    char *output_file = NULL;
    int debug = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0)
        {
            debug = 1;
        }
        else if (!input_file)
        {
            input_file = argv[i];
        }
        else if (!output_file)
        {
            output_file = argv[i];
        }
        else
        {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!input_file || !output_file)
    {
        fprintf(stderr, "Usage: %s [-d] input.bbx output.bcx\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(input_file, "rb");
    if (!in)
    {
        perror("fopen input");
        return 1;
    }

    char header[8] = {0};
    size_t n = fread(header, 1, 5, in);
    rewind(in);

    if (n >= 4 && strncmp(header, "%asm", 4) == 0)
    {
        fclose(in);
        if (debug)
        {
            printf("Debug mode ON\n");
            printf("[DEBUG] Input file: %s\n", input_file);
            printf("[DEBUG] Output file: %s\n", output_file);
        }
        int result = assemble_file(input_file, output_file, debug);
        if (result == 0)
            printf("Assembly successful.\n");
        return result;
    }
    else
    {
        // do the language stuff here
        fclose(in);
        fprintf(stderr, "Input file does not start with %%asm. No assembly performed.\n");
        return 1;
    }
}