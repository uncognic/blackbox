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
- `FOR <name> = <expr> TO <expr> [STEP <expr>]` ... `NEXT [name]`
	(or inline declaration: `FOR VAR <name> = ...`)
- `BREAK` (exits innermost loop)
- `HALT [OK|BAD|number]`
- `LABEL, CALL, RETURN, GOTO`
- `INPUT name` (reads an integer or string from input, stores in variable)
- `INC name` (increments variable)
- `DEC name` (decrements variable)
- `RANDOM name, min, max` (stores random integer in variable)
- `SLEEP time` (sleeps for given milliseconds, or if given a register, sleeps for that many milliseconds stored in the register)
- `EXEC command, result` (executes system command, stores exit code in result variable)
- `GETKEY name` (non-blocking, stores key code of pressed key or -1 if no key is pressed)
- Inline assembly block: `ASM:` ... `ENDASM`

Expressions:
- Integer literals and variables
- Operators: `+`, `-`, `*`, `/`, `%`
- Parentheses for grouping
- Comparison operators in conditions: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Bitwise operators: `&`, `|`, `^`, `~`

Data model:
- Integers (64-bit VM register/slot model)
- Strings (stored in `%data`, loaded/printed as string pointers)

## Not full parity with assembly
BASIC is currently not fully featured compared to the assembly pathway. It will be at some point, but for now you may find that some things are easier to do in assembly. 

You can still drop to assembly for specific instructions using `ASM:` blocks.

## Examples
See examples in the `examples/basic/` directory

## Other
- BASIC syntax reference: `docs/basic/SYNTAX.md`
