# Blackbox BASIC Documentation
## Overview
Blackbox BASIC is a high-level frontend that compiles `.bbs` source into Blackbox bytecode (`.bcx`) through the same toolchain as assembly.

You compile them with the same `bbxc` compiler, and they run on the same `bbx` interpreter. BASIC is designed to be a simpler way to write programs for Blackbox, while still allowing you to drop down to assembly when you need more control.

## Current feature set
Supported statements and blocks:
- `CONST name = <expr or "string">`
- `VAR name = <expr or "string">`
- `name = <expr>`
- `PRINT [value[, value...]]`
- `WRITE [value[, value...]]`
- `IF <condition>:` ... `ELSE:` ... `ENDIF`
- `WHILE <condition>:` ... `ENDWHILE`
- `HALT [OK|BAD|number]`
- Inline assembly block: `ASM:` ... `ENDASM`

Expressions:
- Integer literals and variables
- Operators: `+`, `-`, `*`, `/`, `%`
- Parentheses for grouping
- Comparison operators in conditions: `==`, `!=`, `<`, `<=`, `>`, `>=`

Data model:
- Integers (64-bit VM register/slot model)
- Strings (stored in `%data`, loaded/printed as string pointers)

## Not full parity with assembly
BASIC is intentionally a subset frontend. Assembly still exposes lower-level VM features that BASIC does not model directly, including:
- Manual section/label control
- Macros
- Privilege/syscall/fault instructions
- Direct low-level memory and device-style instruction use

You can still drop to assembly for specific instructions using `ASM:` blocks.

## Examples
See examples in the `examples/basic/` directory

## Other
- BASIC syntax reference: `docs/basic/SYNTAX.md`
