# Blackbox Assembly Syntax
## File structure
Every file must follow this order:
```
%asm

%data
    ; data definitions (optional)

%main
    ; code
```

- `%asm` must be in every assembly file. macro definitions are here.
- `%data` is optional, must come before `%main`/`%entry`
- `%main` and `%entry` are interchangeable — both define the program entry point

## Includes
You can inline other source files with:

```
%include "path/to/file.bbx"
```

- Includes are expanded before macro parsing, so included files can provide `%macro` definitions.
- Relative include paths are resolved from the directory of the file that contains the `%include`.
- Includes can be nested (maximum depth: 32).
- `%include` accepts one quoted path and nothing else on the line (aside from whitespace and comments).

## Sections
### %data
Holds named constants. Only `STR`, `BYTE`, `WORD`, `DWORD`, and `QWORD` definitions are allowed here.

### %main / %entry
Holds executable instructions and labels. Data definitions are not allowed here.

## Instructions
Instructions are written one per line. Syntax is Intel-style with spaces and commas:

```
MOVI R01, 42
ADD R00, R01
JMP some_label
JMP R10
JMP 128
```

Instruction names are case-insensitive. `MOVI`, `movi`, and `Movi` are all valid.

## Registers
- 99 general-purpose registers: `R00` through `R98`
- Register names are case-insensitive (`R01` and `r01` are the same)
- Single-digit registers must be zero-padded: use `R01`, not `R1`

## Labels
Labels are defined with a leading period and trailing colon:

```
.my_label:
    MOVI R00, 1
```

Labels are referenced without the period or colon:

```
JMP my_label
JE  my_label
```

`JMP` accepts a register (`JMP R10`), label (`JMP my_label`), or absolute u32 immediate (`JMP 128`).

Label names are case-sensitive.

## Macros
Macros are defined in the `%asm` section (before `%data` or `%main`):

```
%macro NAME param1 param2
    MOV $dst, $src
    MOVI R07, 40
%endmacro
```

- Parameters are space-separated on the `%macro` line
- Inside the body, parameters are referenced as `$paramname` or positionally as `$1`, `$2`, etc.
- Invocation arguments are comma-or-space separated:
  ```
  %NAME R00, R01
  ```
- Macros can call other macros (nested expansion, max depth 32)
- Local labels inside macros use `@@name` syntax — each expansion gets a unique prefix to avoid collisions:
  ```
  %macro EXAMPLE
      JL @@done
  .@@done:
  %endmacro
  ```
  The `.` goes on the label definition; the jump target is written without the `.`:
  ```
  JL @@done   ; jump target
  .@@done:    ; label definition
  ```
- Up to 32 parameters per macro

## File descriptors
File descriptors are written as `F<n>` (e.g. `F0`, `F1`):

```
FOPEN r, F0, "data.bin"
FREAD F0, R00
FCLOSE F0
```

## Numeric literals
Immediate values support C-style literals:

- Decimal: `42`
- Hex: `0xFF`
- Character: `'A'`
- Negative: `-1`

## Comments
Comments start with `;`:

```
MOVI R00, 0  ; initialize counter
```