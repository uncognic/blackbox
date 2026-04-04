# Blackbox BASIC Syntax

## Source format
- One statement per line.
- Keywords are case-insensitive (`print`, `PRINT`, and `Print` are all accepted). However, the convention is to use uppercase.
- Comments use `//` and run to end of line.
- Strings use double quotes.

Example:

```basic
// comment
VAR X = 5
WRITE "X=" // use WRITE to avoid newline
PRINT X
```

## Variables and constants
### Variable declaration
```basic
VAR INITIAL_VALUE = 10
VAR OTHER_VALUE = INITIAL_VALUE + 5
VAR NAME = "Blackbox"
```

### Constant declaration
```basic
CONST limit = 10
CONST banner = "Blackbox"
```

### Assignment
```basic
name = expression
```

## Expressions
Supported expression atoms:
- Integer literals (for example: `0`, `42`, `-5`)
- Variable names
- Parenthesized expressions

Supported arithmetic operators:
- `*`, `/`, `%` (higher precedence)
- `+`, `-` (lower precedence)

Example:

```basic
VAR A = 2
VAR B = 3
VAR C = (A + B) * 4
```

## Output
### PRINT
Prints values and always appends a newline.

```basic
PRINT
PRINT "value="
PRINT X
PRINT "x=", X
```

### WRITE
Prints values without appending a newline.

```basic
WRITE "x=", X
```

## Conditions and loops
### IF / ELSE / ENDIF
```basic
IF X > 10:
    PRINT "big"
ELSE:
    PRINT "small"
ENDIF
```

### WHILE / ENDWHILE
```basic
WHILE X != 0:
    PRINT X
    X = X - 1
ENDWHILE
```

### FOR / NEXT
```basic
FOR I = 10 TO 0 STEP -1
    PRINT I
NEXT I
```

You can also declare the loop variable inline:
```basic
FOR VAR I = 10 TO 0 STEP -1
    PRINT I
NEXT I
```

Notes:
- `STEP` is optional and defaults to `1`.
- `NEXT` may be used as `NEXT` or `NEXT <var>`.
- Positive and negative steps are both supported.

Condition operators:
- `==`, `!=`, `<`, `<=`, `>`, `>=`
- Logical chaining: `AND`, `OR` (case-insensitive)

Example with logical chaining:

```basic
IF X > 0 AND X < 10 OR X == 42:
    PRINT "matched"
ENDIF
```

## Program termination
```basic
HALT
HALT OK
HALT BAD
HALT 3
```

## Inline assembly
Use an assembly block when you need instructions not exposed by BASIC statements.

```basic
ASM:
    MOVI R01, 65 ; Assembly syntax rules apply here
    PRINTCHAR R01 ; Like these comments
ENDASM
```

Notes:
- Registers 0-15 are used by the BASIC compiler as scratch storage. Nothing is stopping you from using them, but be aware that BASIC statements may overwrite them.
- ASM block lines are passed to the assembler.
- Use assembly syntax rules inside the block.


## Current limitations
- No user-defined functions/procedures in BASIC syntax.
- No arrays or structured types in BASIC syntax.
- Advanced VM privilege/syscall/fault flows are assembly-first.
