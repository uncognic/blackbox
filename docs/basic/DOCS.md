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
- `EPRINT [value[, value...]]` (same as PRINT, but to stderr)
- `EWRITE [value[, value...]]` (same as WRITE, but to stderr)
- `IF <condition>:` ... `ELSE:` ... `ENDIF`
- `WHILE <condition>:` ... `ENDWHILE`
- `FOR <name> = <expr> TO <expr> [STEP <expr>]` ... `NEXT [name]`
	(or inline declaration: `FOR VAR <name> = ...`)
- `BREAK` (exits innermost loop)
- `CONTINUE` (skips to next iteration of innermost loop)
- `HALT [OK|BAD|number]`
- `LABEL, CALL, RETURN, GOTO`
- `FUNC name: VAR arg1, STR arg2, &ref, ...` ... `ENDFUNC` (define a BASIC function)
- `CALL name(expr1, expr2, ...)` (call a BASIC function; functions return a value to be used in expressions)
- `INPUT name` (reads an integer or string from input, stores in variable)
- `INC name` (increments variable)
- `DEC name` (decrements variable)
- `RANDOM name, min, max` (stores random integer in variable)
- `SLEEP time` (sleeps for given milliseconds, or if given a register, sleeps for that many milliseconds stored in the register)
- `EXEC command, result` (executes system command, stores exit code in result variable)
- `GETKEY name` (non-blocking, stores key code of pressed key or -1 if no key is pressed)
- `GETARGC name` (stores the interpreter argument count in integer variable)
- `GETARG name, index` (stores argv[index] into string variable)
- `GETENV name, "VAR"` (stores environment variable value into a string variable)
- `FOPEN mode, handle, "filename"` (opens a file into an integer handle variable)
- `FCLOSE handle` (closes the opened file)
- `FREAD handle, var` (reads one byte from the file into an integer variable, returns -1 on EOF)
- `FWRITE handle, expr` (writes the low byte of an integer expression to the file)
- `FSEEK handle, offset` (seeks the file position to the given offset)
- `FPRINT handle, "text"` or `FPRINT handle, expr` (writes a value to the file and appends a newline)
- Inline assembly block: `ASM:` ... `ENDASM`
An optional entry point can be declared with `@ENTRY`. Execution will start from there.

Expressions:
- Integer literals and variables
- Operators: `+`, `-`, `*`, `/`, `%`
- Parentheses for grouping
- Comparison operators in conditions: `==`, `!=`, `<`, `<=`, `>`, `>=`
- Bitwise operators: `&`, `|`, `^`, `~`

Data model:
- Integers (64-bit VM register/slot model)
- Strings (stored in `%data`, loaded/printed as string pointers)

Code strcutre:
- Namespacing with NAMESPACE blocks.
## Not full parity with assembly
BASIC is currently not fully featured compared to the assembly pathway. It will be at some point, but for now you may find that some things are easier to do in assembly. 

You can still drop to assembly for specific instructions using `ASM:` blocks.

## Examples
See examples in the `examples/basic/` directory

## Other
- BASIC syntax reference: `docs/basic/SYNTAX.md`
