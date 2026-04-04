#ifndef ASM_H
#define ASM_H

#ifdef __cplusplus
extern "C" {
#endif

int assemble_file(const char *filename, const char *output_file, int debug);

#ifdef __cplusplus
}
#endif

#endif