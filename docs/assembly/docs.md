# Blackbox Assembly Documentation
### Info
- Syntax is Intel-assembly like: instructions use spaces and commas (e.g. `MOVI R01, 42`).
- Labels start with a period and end with a colon (`.label:`), and are referenced without the period (`JMP label`).
- All files must start with `%asm`.
- All files must have a `%main` or `%entry` section for the program entry point.
- Data definitions must be in the `%data` section, before the entry point.
- Macro definitions must be in the `%asm` section
- Registers: `R00`-`R98` (99 total). 
- The assembly is case-insensitive for instructions and register names. Label and macro names are case-sensitive.

### Instruction set
See [isa.md](isa.md)

### Syntax reference
See [SYNTAX.md](SYNTAX.md)