# Blackbox Assembly Syntax

## File structure

Every assembly file must follow this order:
```asm
%asm
; optional: macro definitions
%globals <n>    ; optional: declare N global variable slots
%data
; optional: string constant definitions
%main
; code
```
- `%asm` must appear at the top of every file.
- Macro definitions go in the `%asm` section before any other sections.
- `%globals N` must appear before `%data` or `%main` if used.
- `%data` is optional and must come before `%main`/`%entry`.
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
MOVI R00, $MAX
```
- By convention, names use `$` (for example `$MAX`).
- Substitution is purely textual, done before assembly.
- Works for numbers, strings, register names, or any token.

## Sections

### %data

Holds string constant definitions. Only `STR` is allowed here.
```asm
%data
STR $greeting, "Hello, world!"
STR $prompt, "Enter a number: "
```

### %main / %entry

Holds executable instructions and labels. No data definitions allowed here.

## Instructions

One instruction per line. Intel-style syntax with spaces and commas:
```asm
MOVI R01, 100
ADD  R00, R01
JMP  R05
JMP some_label
```
Instruction names are case-insensitive but it is recommended to use uppercase.

## Registers

- 99 general-purpose registers: `R00` through `R98`.
- Register names are case-insensitive (`R01` and `r01` are equivalent).
- Single-digit registers must be zero-padded: use `R01`, not `R1`.
- `R00` is the conventional return value register for function calls.

## Labels

Labels are defined with a leading period and trailing colon:

```asm
.my_label:
MOVI R00, 1
```

Referenced without the period or colon:
```asm
JMP my_label
JE   my_label
```

`JMP` accepts a register, a label, or a numeric immediate.
Label/immediate jump forms are encoded as the VM's `JMPI` opcode internally.

Label names are case-sensitive.

## Frame sizes

After a label that begins a function, declare the number of local variable slots it needs:
```asm
.my_func:
  FRAME 3
  ; function body using LOADVAR/STOREVAR slots 0, 1, 2
  RET
```
`FRAME` is assembler-only and emits nothing. The assembler records the frame size and encodes it into `CALL` instructions targeting this label.

## Global variables

Declare the global segment size at the top of the file:

```asm
%globals 10
```

Access global slots from any function using `LOADGLOBAL`/`STOREGLOBAL`:
```asm
MOVI R01, 100   ; load immediate 100 into R01
STOREGLOBAL R01, 0    ; store R01 into global slot 0
LOADGLOBAL R02, 0     ; load global slot 0 into R02
```

Global slots are initialized to zero at program start and shared across all call frames.

## File descriptors

Written as `F<n>`:
```asm
FOPEN w, F3, "output.txt"
FWRITE F3, R00
FCLOSE F3
```

Standard streams are also accepted as descriptors: `STDIN`, `STDOUT`, `STDERR`.

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
  MOVI R97, 0
  CMP  $reg, R97
  JGE  @@done
  MOVI R97, -1
  MUL  $reg, R97
.@@done:
%endmacro
```

The `.` prefix belongs on the label definition only. Jump targets omit it.

## Numeric literals

- Decimal: `42`
- Negative: `-1`

Most instruction immediates currently accept decimal integers. Character literals are supported by `PRINT`.

## Comments

Start with `;` and extend to end of line:
```asm
MOVI R00, 0   ; initialize counter
; this whole line is a comment
```

## Calling convention

There is no enforced calling convention. The common pattern used by the BASIC compiler:

- Arguments are pushed onto the operand stack before `CALL`, in order.
- The callee pops them with `POP`.
- Return value goes in `R00`.
- The callee uses `LOADVAR`/`STOREVAR` for local variables within its frame.

Example:
```asm
; caller
MOVI R01, 10
PUSH R01
MOVI R01, 20
PUSH R01
CALL add_them
; callee
.add_them:
  FRAME 2
  POP  R01
  STOREVAR R01, 0
  POP  R02
  STOREVAR R02, 1
  LOADVAR R01, 0
  LOADVAR R02, 1
  ADD  R01, R02
  MOV  R00, R01
  RET
```