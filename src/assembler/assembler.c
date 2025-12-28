#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../interpreter/opcodes.h"
#define BCX_VERSION 1
static char *trim(char *s) {
    while (isspace(*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace(*end)) *end-- = '\0';
    return s;
}
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s input.bbx output.bcx\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        perror("fopen input");
        return 1;
    }

    FILE *out = fopen(argv[2], "wb");
    if (!out) {
        perror("fopen output");
        fclose(in);
        return 1;
    }
    

    char line[256];
    int lineno = 0;

    while (fgets(line, sizeof(line), in)) {
        lineno++;
        char *s = trim(line);
        if (*s == '\0' || *s == ';')
            continue;
        
        if (strncmp(s, "PRINT", 5) == 0) {
            char c;
            if (sscanf(s + 5, " '%c", &c) != 1) {
                fprintf(stderr, "Syntax error on line %d\n", lineno);
                return 1;
            }
            fputc(OPCODE_PRINT, out);
            fputc((uint8_t)c, out);
        }

        else if (strcmp(s, "NEWLINE") == 0) {
            fputc(OPCODE_NEWLINE, out);
        }

        else if (strcmp(s, "HALT") == 0) {
            fputc(OPCODE_HALT, out);
        }
        else {
            fprintf(stderr, "Unknown instruction on line %d: %s\n", lineno, s);
            return 1;
        }

    }

    fclose(in);
    fclose(out);
    return 0;
}