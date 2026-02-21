# Blackbox assembly documentation
### Info
- File magic: 3 bytes 0x62 0x63 0x78 ("bcx") at program start.
- Syntax is Intel-assembly like: instructions use spaces and commas (e.g. MOV R01, 42). Labels start with a period and end with a colon (.label:).
- All files must have a %asm statement at the very top of the file.
- All files must have a %main or %entry section for the program entry point. 
- Data definitions must be in the %data section, before the entry point.
- Registers: R00–R98 (99 total). CMP result/flag is stored in R98.
- Stack: heap-backed array of int64_t elements. Initially 16384 elements. The stack capacity can be changed explicitly via ALLOC, GROW, RESIZE and FREE; the interpreter maintains stack capacity in elements and reallocates the backing buffer on those operations.
- Immediate values encoded as 32-bit little-endian.
- Filenames are limited to a maximum length of 255 characters, since their length is stored in a single byte.
- All file reads/writes are raw binary (no text mode translation).
- The assembler enforces register names as R followed by a decimal index (00–98). Use zero-padded forms like R01 for single-digit registers when needed.
- File descriptors in assembler are specified as F<n> (e.g. F1). The assembler validates descriptor numbers.
- Immediate parsing supports C-style numeric literals (decimal, hex 0x, etc.).
### Instruction set
See [isa.md](isa.md)