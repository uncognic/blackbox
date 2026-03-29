# Blackbox Assembly Documentation
## Info
- Syntax is Intel-assembly like: instructions use spaces and commas (e.g. `MOVI R01, 42`).
- Labels start with a period and end with a colon (`.label:`), and are referenced without the period (`JMP label`).
- All files must start with `%asm`.
- All files must have a `%main` or `%entry` section for the program entry point.
- Data definitions must be in the `%data` section, before the entry point.
- Macro definitions must be in the `%asm` section
- Registers: `R00`-`R98` (99 total). 
- The assembly is case-insensitive for instructions and register names. Label and macro names are case-sensitive.

## Privilege system
Blackbox has two execution modes:

- PRIVILEGED: Can execute all instructions including memory management (`ALLOC`, `GROW`, `RESIZE`, `FREE`), file operations (`FOPEN`, `FCLOSE`), and system commands (`EXEC`). The VM boots in this mode.
- PROTECTED: Cannot execute privileged instructions. Can request privileged operations via `SYSCALL`, whose handlers will have been set by the PRIVILEGED code

See the ISA for privilege instructions (`REGSYSCALL`, `SETPERM`, `DROPPRIV`, `SYSCALL`, `SYSRET`).
### Startup sequence
The VM always starts in PRIVILEGED mode. The typical setup pattern is:

1. Register syscall handlers with `REGSYSCALL`
2. Optionally configure memory permissions with `SETPERM`
3. Drop to PROTECTED mode with `DROPPRIV`
4. `JMP` to protected code

But you can also just run everything in PRIVILEGED mode if you want.

### Syscall convention
Protected code requests privileged operations by calling `SYSCALL <id>`. The VM elevates to PRIVILEGED mode and jumps to the handler registered for that id. The handler performs the privileged work and returns control with `SYSRET`, which drops back to PROTECTED mode and resumes execution after the `SYSCALL` instruction.

Arguments can be passed to handlers via registers by convention (e.g. `R00`=arg0, `R01`=arg1).

### Fault handling
If a protected instruction is attempted in PROTECTED mode, or if a memory access violates permissions, the VM raises a fault. You can define handlers for faults with `REGFAULT` and the VM will jump to them when faults occur, passing the fault code as an argument. This allows protected code to handle faults gracefully.

## Instruction set
See [isa.md](isa.md)

## Syntax reference
See [SYNTAX.md](SYNTAX.md)