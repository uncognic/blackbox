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
### Array declaration
```basic
VAR arr[10]
arr[0] = 1
```
### Constant declaration
```basic
CONST limit = 10
CONST banner = "Blackbox"
```

### Global declarations
```basic
GLOBAL VAR counter = 0
GLOBAL CONST app_name = "Blackbox"
```
`GLOBAL VAR` allocates storage in the VM global segment. Name visibility still follows BASIC scope resolution.
`GLOBAL CONST` is currently accepted and behaves like `CONST`.

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
- `<<`, `>>` (shift operators)

Supported bitwise operators:
- `&`, `|`, `^`
- Unary `~`

Example:

```basic
VAR A = 2
VAR B = 3
VAR C = (A + B) * 4
```

## Output
### PRINT
Prints values and always appends a newline. It can also evaluate expressions

```basic
PRINT
PRINT "value="
PRINT X
PRINT "x=", X
```
```basic
VAR test = 10
PRINT test + 5
// output: 15
```

### WRITE
Prints values without appending a newline.

```basic
WRITE "x=", X
```

### EWRITE
Same as `WRITE`, but sends output to `stderr`.

```basic
EWRITE "error: x=", X
```

### EPRINT
Same as `PRINT`, but sends output to `stderr` and appends a newline.

```basic
EPRINT "fatal: invalid value"
EPRINT "x=", X
```

## File I/O
### FOPEN / FCLOSE
Open and close a file using an integer handle variable.

```basic
VAR fh = 0
FOPEN r, fh, "input.txt"
FWRITE fh, 65
FCLOSE fh
```

### FREAD
Read a single byte from a file.

```basic
VAR fh = 0
VAR value = 0
FOPEN r, fh, "input.txt"
FREAD fh, value
FCLOSE fh
```

### FWRITE
Write a single byte value to a file.

```basic
FWRITE fh, 65
```

### FSEEK
Seek to a file offset.

```basic
FSEEK fh, 0
```

### FPRINT
Write bytes to a file and append a newline byte (`10`).

```basic
FPRINT fh, "hello"
FPRINT fh, 10
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
### ELSE IF
```basic
IF y < 5:
    PRINT "y is less than 5"
ELSE IF y > 5:
    PRINT "y is greater than 5"
    IF y == 10:
        PRINT "y is exactly 10"
    ELSE IF y == 15:
        PRINT "y is exactly 15"
    ELSE:
        PRINT "y is something else"
    ENDIF
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

### FOREACH / NEXT
```basic
VAR arr[10]
arr[0] = 1
arr[1] = 2
arr[2] = 3
arr[3] = 4
arr[4] = 5

FOREACH VAR i IN arr:
    PRINT i
NEXT i
```

You can also declare the loop variable inline:
```basic
FOR VAR I = 10 TO 0 STEP -1
    PRINT I
NEXT I
```

## Labels
```basic
CALL program
HLT OK

LABEL program
    PRINT "In program"
    RETURN
```
You can also use GOTO if you don't want to return.

## INPUT 
```basic
VAR test = 0
INPUT "Enter a number: ", test

VAR string = ""
INPUT "Enter a string: ", string

WRITE "You entered the number: "
PRINT test

WRITE "You entered the string: "
PRINT string
```
## Functions
### Function definition
```basic
FUNC add: VAR a, VAR b
    RETURN a + b
ENDFUNC
```
- `FUNC <name>:` begins a function definition.
- Parameters are declared after the colon using `VAR` for integer parameters and `STR` for string parameters.
- Functions end with `ENDFUNC`.

### Reference parameters
You can also pass parameters by reference using `&`:
```basic
// This should print 20 and then 10
FUNC swap: &a, &b
    VAR tmp = a
    a = b
    b = tmp
ENDFUNC

@ENTRY
VAR x = 10
VAR y = 20
CALL swap(x, y)
PRINT x
PRINT y
```
### Function calls
```basic
VAR sum = add(3, 4)
PRINT sum
```
- `CALL name(arg1, arg2, ...)` invokes a BASIC function explicitly.
- A function can also be called inside expressions: `VAR result = add(x, y) * 2`
- Function return values are provided through `RETURN <expr>`.

You can also pass a string directly as a parameter:
```basic
NAMESPACE namespace
	FUNC print: STR msg
		PRINT msg
	ENDFUNC
ENDNAMESPACE

@ENTRY

// This should print Hello, World!
CALL namespace.print("Hello, World!")
```
### RETURN
```basic
RETURN value
```
- `RETURN <expr>` inside a function returns an integer value.
- A bare `RETURN` without an expression is also allowed, and simply returns to the caller.

## INC / DEC
```basic
FOR VAR I = 10 TO 0 STEP 0
    DEC I
NEXT I
```
This example uses `DEC` instead of `STEP -1` to count down. You can also use `INC` to count up.
Notes:
- `STEP` is optional and defaults to `1`.
- `NEXT` may be used as `NEXT` or `NEXT <var>`.
- Positive and negative steps are both supported.

## BREAK
```basic
WHILE 0 == 0:
    PRINT "This will print once, then break."
    BREAK
ENDWHILE
```
Condition operators:
- `==`, `!=`, `<`, `<=`, `>`, `>=`
- Logical chaining: `AND`, `OR` (case-insensitive)

Example with logical chaining:

```basic
IF X > 0 AND X < 10 OR X == 42:
    PRINT "matched"
ENDIF
```

## RANDOM
```basic
VAR X = 0;
FOR VAR I = 0 TO 10
    RANDOM X, 0, 100
    PRINT "Random number: ", X
NEXT I
HLT OK
```
```basic
VAR test = -1
WHILE test == -1:
    GETKEY test
    SLEEP 100
ENDWHILE
PRINT "Key code: ", test
```

## GETARGC
```basic
VAR argc = 0
GETARGC argc
PRINT "Argument count: ", argc
```
Returns the number of command-line arguments available in the interpreter runtime.

## GETARG
```basic
VAR arg0 = ""
GETARG arg0, 0
PRINT "argv[0] = ", arg0
```
Retrieves the commandline argument at the given zero-based index and stores its string pointer into a string variable.

- `GETARG 0` returns the interpreter executable path.
- `GETARG 1` returns the program path.
- `GETARG 2` returns the first user argument.

`index` is currently parsed as an integer literal.

## GETENV
```basic
VAR env_path = ""
GETENV env_path, "PATH"
PRINT "PATH=", env_path
```
Reads the named environment variable and stores its value in a string variable.

If the variable is not defined, the interpreter raises `FAULT_ENV_VAR_NOT_FOUND`, which should be set up with a handler if you want to handle it gracefully.

## Program termination
```basic
HLT
HLT OK
HLT BAD
HLT 3
```

## Inline assembly
Use an assembly block when you need instructions not exposed by BASIC statements.

```basic
ASM:
    MOV R1, 65 ; Assembly syntax rules apply here
    PRINTCHAR R1 ; Like these comments
ENDASM
```

### Setting up handlers
```basic
ASM:
    ; Register fault 6 (FAULT_ENV_VAR_NOT_FOUND) to handler_fault
    REGFAULT 6, handler_fault
    
    ; Jump to protected code
    DROPPRIV
    JMP protected_start

    ; Handlers
        .handler_fault:
            ; Handle the environment variable not found fault by printing a message and returning
            WRITE STDOUT "Caught an environment variable not found fault!"
            NEWLINE
            FAULTRET
    .protected_start:
ENDASM
// BASIC code continues here
```
BASIC code can continue after the ASM block in protected mode.

### Namespaces
You can also use namespaces to organize your code and avoid name collisions.
```basic
NAMESPACE namespace
	FUNC print: STR msg
		// This should print "Hello, World!"
		PRINT msg
	ENDFUNC
ENDNAMESPACE

@ENTRY

VAR hello = "Hello, World!"
CALL namespace.print(hello)
```

### Includes
You can include files to split your code into multiple files or to include libraries. Paths are relative to the including file. Typically, you would wrap the included file in a namespace to avoid name collisions.

```basic
// math.bbs
NAMESPACE Math
    FUNC abs: VAR n
            IF n < 0:
            RETURN 0 - n
        ENDIF
        RETURN n
    ENDFUNC

    FUNC max: VAR a, VAR b
        IF a > b:
            RETURN a
        ENDIF
        RETURN b
    ENDFUNC
ENDNAMESPACE
```
```basic
// main.bbs
INCLUDE "math.bbs"

VAR x = Math.abs(-10) // should print 10
PRINT x
```

## Notes:
- `R1`-`R15` are used by the BASIC compiler as scratch registers (`R0` is used for function return values). Nothing prevents using them manually (such as in ASM blocks) but BASIC statements may overwrite them.
- ASM block lines are passed to the assembler.
- Use assembly syntax rules inside the block.


## Current limitations
- Advanced VM privilege/syscall/fault flows are assembly-first.
