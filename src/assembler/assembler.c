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
        
        if (strncmp(s, "WRITE", 5) == 0) {
            int fd;
            char *str_start;

            if (sscanf(s + 5, " %d", &fd) != 1) {
                fprintf(stderr, "Syntax error on line %d: expected WRITE <fd> '<char>'\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            if (fd != 1 && fd != 2) {
                fprintf(stderr, "Invalid file descriptor on line %d: %d (only 1=stdout, 2=stderr allowed)\n", lineno, fd);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start = strchr(s, '"');
            if (!str_start) {
                fprintf(stderr, "Syntax error on line %d: missing opening quote for string\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            str_start++;

            char *str_end = strchr(str_start, '"');
            if (!str_end) {
                fprintf(stderr, "Syntax error on line %d: missing closing quote for string\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            size_t len = str_end - str_start;
            if (len > 255) len = 255;

            fputc(OPCODE_WRITE, out);
            fputc((uint8_t)fd, out);
            fputc((uint8_t)len, out);

            for (size_t i = 0; i < len; i++) {
                fputc((uint8_t)str_start[i], out);
            }
        }

        else if (strcmp(s, "NEWLINE") == 0) {
            fputc(OPCODE_NEWLINE, out);
        }

        else if (strcmp(s, "HALT") == 0) {
            fputc(OPCODE_HALT, out);
        }
        else if (strncmp(s, "PRINT", 5) == 0) {
            char c;
            if (sscanf(s + 5, " '%c", &c) != 1) {
                fprintf(stderr, "Syntax error on line %d\n", lineno);
                fclose(in);
                fclose(out);
                return 1;
            }
            fputc(OPCODE_PRINT, out);
            fputc((uint8_t)c, out);
        }
        else {
            fprintf(stderr, "Unknown instruction on line %d: %s\n", lineno, s);
            fclose(in);
            fclose(out);
            return 1;
        }

    }

    fclose(in);
    fclose(out);
    return 0;
}