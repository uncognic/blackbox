# Blackbox Assembly Documentation

## Info
- Syntax is Intel-assembly like: instructions use spaces and commas (e.g. `MOV R1, 42`).
- Labels start with a period and end with a colon (`.label:`), and are referenced without the period (`JMP label`). `JMP` accepts registers, labels, and numeric immediates (label/immediate forms are encoded as `JMPI`).
- All files must start with `.asm`.
- All files must have a `.main` or `.entry` section for the program entry point.
- Data definitions (string constants) must be in the `.data` section, before the entry point.
- Uninitialized global variable slots are declared in the `.bss` section, before `.data` or `.main`.
- Macro definitions must be in the `.asm` section.
- Registers: `R0`-`R98` (99 total).
- The assembly is case-insensitive for instructions and register names. Label and macro names are case-sensitive.

## MOV and typed operands
`MOV` is the universal data movement instruction. The assembler encodes operand types into the binary, so the VM knows at runtime what each operand is. Valid forms:

```asm
MOV R1, 67          ; register - immediate
MOV R1, R2         ; register - register
MOV R1, [counter]   ; register - bss slot (deref)
MOV [counter], R1   ; bss slot - register
MOV [counter], 67    ; bss slot - immediate
MOV R1, counter     ; register - bss slot index (address-of)
MOV R1, VAR 0       ; register - frame-local slot 0
MOV VAR 0, R1       ; frame-local slot 0 - register
```

## BSS section
Declare named uninitialized global slots:

```asm
.bss
    counter
    total
```

Slots are zero-initialized at startup. Reference them by name with brackets for value access, or bare for address-of (slot index).

## Privilege system
Blackbox has two execution modes:

- **PRIVILEGED**: Can execute all instructions including memory management (`ALLOC`, `GROW`, `RESIZE`, `FREE`), file operations (`FOPEN`, `FCLOSE`), and system commands (`EXEC`). The VM boots in this mode.
- **PROTECTED**: Cannot execute privileged instructions. Can request privileged operations via `SYSCALL`, whose handlers will have been registered by privileged code.

See the ISA for privilege instructions (`REGSYSCALL`, `SETPERM`, `DROPPRIV`, `SYSCALL`, `SYSRET`).

### Startup sequence
The VM always starts in PRIVILEGED mode. The typical setup pattern is:

1. Register syscall handlers with `REGSYSCALL`
2. Optionally configure memory permissions with `SETPERM`
3. Drop to PROTECTED mode with `DROPPRIV`
4. `JMP` to protected code

You can also run everything in PRIVILEGED mode.

### Syscall convention
Protected code requests privileged operations via `SYSCALL <id>`. The VM elevates to PRIVILEGED and jumps to the registered handler. The handler performs the privileged work and returns with `SYSRET`, dropping back to PROTECTED and resuming after the `SYSCALL`.

Arguments are passed via registers by convention (e.g. `R0`=arg0, `R1`=arg1).

### Fault handling
If a privileged instruction is attempted in PROTECTED mode, a memory access violates permissions, or any other fault condition occurs, the VM raises a fault. Register handlers with `REGFAULT` to handle faults gracefully. See `fault.hpp` for fault types.

## Instruction set
See [ISA.md](ISA.md)

## Syntax reference
See [SYNTAX.md](SYNTAX.md)