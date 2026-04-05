#pragma once

#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cctype>
#include <string>


#include "define.h"
namespace blackbox {
namespace tools {

bool preprocess_includes(const std::string &input, std::string &out);

uint32_t find_data(const char *name, Data *data, size_t count);
uint32_t find_label(const char *name, Label *labels, size_t count);

size_t instr_size(const char *line);
uint8_t parse_register(const char *r, int lineno);
char *trim(char *s);
uint8_t parse_file(const char *r, int lineno);
uint64_t get_true_random();

Macro *find_macro(Macro *macros, size_t macro_count, const char *name);
int expand_invocation(const char *invocation_line, FILE *dest, int depth, Macro *macros, size_t macro_count, unsigned long *expand_id);
int equals_ci(const char *a, const char *b);
int starts_with_ci(const char *s, const char *prefix);

std::string replace_all(const std::string &src, const std::string &find, const std::string &repl);

}}