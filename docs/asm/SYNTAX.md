# Blackbox Assembly Syntax

## File structure

Every assembly file must follow this order:
```asm
%asm
; optional: macro definitions
%bss
; optional: named uninitialized global slots
%data
; optional: string constant definitions
%entry
; code
```
- `%asm` must appear at the top of every file.
- Macro definitions go in the `%asm` section before any other sections.
- `%bss` declares named global slots, zero-initialized at startup. Must appear before `%data` or `%entry`.
- `%data` is optional and must come before `%entry`.
- `%main` and `%entry` are interchangeable.

## Includes

Inline another source file:
```asm
%include "path/to/file.bbx"
```
- Includes are expanded before macro parsing. Included files can define macros.
- Paths are resolved relative to the including file's directory.
- Maximum nesting depth: 32.

## Defines

Named textual substitutions:
```asm
%define $MAX 100
%define $GREETING "Hello"
MOV R00, $MAX
```
- By convention, names use `$` (for example `$MAX`).
- Substitution is purely textual, done before assembly.
- Works for numbers, strings, register names, or any token.

## Sections

### %bss

Declares named uninitialized global variable slots. Each name on its own line becomes a slot, assigned indices in order starting from 0.

```asm
%bss
    counter
    total
    flag
```

Access with brackets for value, bare name for slot index:
```asm
MOV [counter], 42      ; store 42 into counter
MOV R01, [counter]     ; load counter into R01
MOV R01, counter       ; load slot index of counter into R01 (address-of)
```

### %data

Holds string constant definitions.
```asm
%data
  greeting "Hello, world!"
  prompt "Enter a number: "
```

### %main / %entry

Holds executable instructions and labels. No data definitions allowed here.

## MOV

`MOV` is the universal data movement instruction. The assembler encodes a type byte for each operand into the binary so the VM knows how to interpret each one at runtime.

```asm
MOV R01, 67          ; register - immediate (i32)
MOV R01, R02         ; register - register
MOV R01, [name]      ; register - bss slot value
MOV [name], R01      ; bss slot - register
MOV [name], 67       ; bss slot - immediate
MOV R01, name        ; register - bss slot index (address-of)
MOV R01, VAR 0       ; register - frame-local slot 0
MOV VAR 0, R01       ; frame-local slot 0 - register
```

## Instructions

One instruction per line. Intel-style syntax with spaces and commas:
```asm
MOV R01, 100
ADD R00, R01
JMP R05
JMP some_label
```
Instruction names are case-insensitive but uppercase is recommended.

## Registers

- 99 general-purpose registers: `R00` through `R98`.
- Register names are case-insensitive (`R01` and `r01` are equivalent).
- Single-digit registers must be zero-padded: use `R01`, not `R1`.
- `R00` is the conventional return value register for function calls.

## Labels

Labels are defined with a leading period and trailing colon:

```asm
.my_label:
MOV R00, 1
```

Referenced without the period or colon:
```asm
JMP my_label
JE  my_label
```

`JMP` accepts a register, a label, or a numeric immediate. Label/immediate forms are encoded as `JMPI` internally.

Label names are case-sensitive.

## Frame sizes

After a label that begins a function, declare the number of local variable slots it needs:
```asm
.my_func:
    FRAME 3
    ; body using MOV VAR 0, MOV VAR 1, MOV VAR 2
    RET
```
`FRAME` is assembler-only and emits nothing. The assembler records the frame size and encodes it into `CALL` instructions targeting this label.

## File descriptors

Written as `F<n>`:
```asm
FOPEN w, F3, "output.txt"
FWRITE F3, R00
FCLOSE F3
```

Standard streams: `STDIN`, `STDOUT`, `STDERR`.

## Macros

Defined in the `%asm` section:
```asm
%macro SWAP dst, src
MOV R97, $dst
MOV $dst, $src
MOV $src, R97
%endmacro
```

Invoked with `%`:
```asm
%SWAP R00, R01
```

- Parameters are space- or comma-separated on the `%macro` line.
- Referenced by name as `$name`.
- Invocation arguments are space or comma separated.
- Macros can call other macros (max nesting depth: 32).
- Local labels inside macros use `@@name` to get unique per-expansion names:

```asm
%macro ABS reg
  MOV R97, 0
  CMP $reg, R97
  JGE @@done
  MOV R97, -1
  MUL $reg, R97
.@@done:
%endmacro
```

The `.` prefix belongs on the label definition only. Jump targets omit it.

## Numeric literals

- Decimal: `42`
- Negative: `-1`

## Comments

Start with `;` and extend to end of line:
```asm
MOV R00, 0   ; initialize counter
; this whole line is a comment
```

## Calling convention

There is no enforced calling convention. The common pattern used by the BASIC compiler:

- Arguments are pushed onto the operand stack before `CALL`, in order.
- The callee pops them with `POP`.
- Return value goes in `R00`.
- The callee uses `MOV VAR N` for local variables within its frame.

Example:
```asm
; caller
MOV R01, 10
PUSH R01
MOV R01, 20
PUSH R01
CALL add_them

; callee
.add_them:
    FRAME 2
    POP R01
    MOV VAR 0, R01
    POP R02
    MOV VAR 1, R02
    MOV R01, VAR 0
    MOV R02, VAR 1
    ADD R01, R02
    MOV R00, R01
    RET
```