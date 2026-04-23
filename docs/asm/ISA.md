# Blackbox VM Instruction Set Architecture

## Data section

### STR

Define a string constant. Must be in the `%data` section.

- Syntax: `STR $<name>, "<contents>"`
- Behavior: Adds a string entry to the data table. Referenced by name with `LOADSTR`.

Numeric data types (`BYTE`, `WORD`, `DWORD`, `QWORD`) have been removed. Use `MOVI` for compile-time numeric constants,
or `%define` for named numeric constants.

## Directives

### %globals

Declare the number of global variable slots for the program.

- Syntax: `%globals <n>`
- Placement: Before `%data` or `%main`.
- Behavior: Allocates `n` slots in the global segment at program startup, initialized to zero. Accessed via
  `LOADGLOBAL`/`STOREGLOBAL`.

### %define

Define a named constant that is substituted textually before assembly.

- Syntax: `%define $<name> <value>`
- Behavior: Every occurrence of `$<name>` in the source is replaced with `<value>` before instruction encoding. Works
  for both numeric and string values.

```
%define $MAX 100
%define $MSG "Hello"
MOVI R00, $MAX
```

### FRAME

Assembler-only directive to declare a label's frame size.

- Syntax: `FRAME <n>` (placed immediately after a label definition)
- Encoding: none (assembler-only)
- Behavior: Records the number of local variable slots the label's function requires. The assembler uses this when
  encoding `CALL <label>` to fill the 4-byte frame size field.

## Output

### WRITE

Write a string literal to a stream.

- Syntax: `WRITE <STDOUT|STDERR> "<string>"`
- Encoding: opcode, 1 byte fd (1=STDOUT, 2=STDERR), 4-byte length, then string bytes.

### NEWLINE

Print a newline character to stdout.

- Syntax: `NEWLINE`
- Encoding: opcode only.

### PRINT

Print a single character literal.

- Syntax: `PRINT '<char>'` or `PRINT <u8>`
- Encoding: opcode, 1 byte char.

### PRINTREG

Print the integer value of a register (decimal, no newline).

- Syntax: `PRINTREG <reg>`
- Encoding: opcode, 1 byte register.

### PRINTSTR

Print the string referenced by a register (loaded via `LOADSTR` or `READSTR`).

- Syntax: `PRINTSTR <reg>`
- Encoding: opcode, 1 byte register.

### PRINTCHAR

Print the ASCII character corresponding to the register value.

- Syntax: `PRINTCHAR <reg>`
- Encoding: opcode, 1 byte register.

### EPRINTREG

Print register value to stderr.

- Syntax: `EPRINTREG <reg>`
- Encoding: opcode, 1 byte register.

### EPRINTSTR

Print string to stderr.

- Syntax: `EPRINTSTR <reg>`
- Encoding: opcode, 1 byte register.

### EPRINTCHAR

Print character to stderr.

- Syntax: `EPRINTCHAR <reg>`
- Encoding: opcode, 1 byte register.

### PRINT_STACKSIZE

Print the current operand stack capacity (in elements).

- Syntax: `PRINT_STACKSIZE`
- Encoding: opcode only.

## Input

### READ

Read an integer from stdin into a register.

- Syntax: `READ <reg>`
- Encoding: opcode, 1 byte register.

### READSTR

Read a line from stdin and store a string handle in a register.

- Syntax: `READSTR <reg>`
- Encoding: opcode, 1 byte register.
- Behavior: Reads characters until newline or EOF. Stores a handle into the runtime string table in the register. Use
  `PRINTSTR` to print it.

### READCHAR

Read a single non-whitespace character from stdin.

- Syntax: `READCHAR <reg>`
- Encoding: opcode, 1 byte register.
- Behavior: Skips leading whitespace, reads one character, stores its ASCII code in the register. On EOF, stores `0`.

## Registers and stack

### MOVI

Move an immediate integer value into a register.

- Syntax: `MOVI <dst>, <value>`
- Encoding: opcode, 1 byte dst, 4-byte signed immediate.

### MOV

Copy one register into another.

- Syntax: `MOV <dst>, <src>`
- Encoding: opcode, 1 byte dst, 1 byte src.

### PUSH

Push a register value onto the operand stack.

- Syntax: `PUSH <reg>`
- Encoding: opcode, 1 byte register.

### PUSHI

Push a signed immediate value onto the operand stack.

- Syntax: `PUSHI <value>`
- Encoding: opcode, 4-byte signed immediate.

### POP

Pop the top of the operand stack into a register.

- Syntax: `POP <reg>`
- Encoding: opcode, 1 byte register.

### CMP

Compare two registers and set flags.

- Syntax: `CMP <reg1>, <reg2>`
- Encoding: opcode, 1 byte reg1, 1 byte reg2.
- Behavior: Computes `reg1 - reg2` and sets ZF, SF, CF, OF, AF, PF accordingly.

## Arithmetic

### ADD / SUB / MUL / DIV / MOD

Binary arithmetic on registers.

- Syntax: `<OP> <dst>, <src>`
- Encoding: opcode, 1 byte dst, 1 byte src.
- Behavior: `dst = dst OP src`. Division by zero raises a fault.

### INC / DEC

Increment or decrement a register by one.

- Syntax: `INC <reg>` / `DEC <reg>`
- Encoding: opcode, 1 byte register.

## Bitwise

### NOT

Bitwise NOT (flips all bits).

- Syntax: `NOT <reg>`
- Encoding: opcode, 1 byte register.

### AND / OR / XOR

Bitwise binary operations.

- Syntax: `<OP> <dst>, <src>`
- Encoding: opcode, 1 byte dst, 1 byte src.
- Behavior: `dst = dst OP src`.

### SHL / SHR

Register-count bitwise shift.

- Syntax: `SHL <dst>, <src>` / `SHR <dst>, <src>`
- Encoding: opcode, 1 byte dst, 1 byte src.
- Behavior: Shifts `dst` left or right by the value in `src`. SHR preserves sign for negative values.

### SHLI / SHRI

Immediate-count bitwise shift.

- Syntax: `SHLI <reg>, <shift>` / `SHRI <reg>, <shift>`
- Encoding: opcode, 1 byte register, 8-byte unsigned shift count.
- Behavior: Shifts the register by the encoded immediate count. SHRI preserves sign for negative values.

## Control flow

### JMP

Unconditional jump using a register, label, or immediate address.

- Syntax: `JMP <reg> | <label> | <u32>`
- Encoding: register form uses opcode `JMP` + 1 byte register. Label/immediate forms are encoded as opcode `JMPI` + 4-byte address.
- Behavior: Jumps to the resolved target address.

### JE / JNE

Conditional jump on zero flag.

- Syntax: `JE <label>` / `JNE <label>`
- Encoding: opcode, 4-byte address.
- Behavior: JE jumps if ZF=1 (last CMP was equal). JNE jumps if ZF=0.

### JL / JGE

Conditional jump on signed comparison.

- Syntax: `JL <label>` / `JGE <label>`
- Encoding: opcode, 4-byte address.
- Behavior (after `CMP reg1, reg2`): JL jumps if SF != OF (reg1 < reg2 signed). JGE jumps if SF == OF.

### JB / JAE

Conditional jump on unsigned comparison.

- Syntax: `JB <label>` / `JAE <label>`
- Encoding: opcode, 4-byte address.
- Behavior (after `CMP reg1, reg2`): JB jumps if CF=1 (reg1 < reg2 unsigned). JAE jumps if CF=0.

### CALL

Call a subroutine.

- Syntax: `CALL <label>`
- Encoding: opcode, 4-byte address, 4-byte frame size.
- Behavior: Pushes the return address onto the call stack. Allocates `frame_size` slots in the local variable region for
  the callee. Jumps to the label. Frame size is taken from the `FRAME` directive at the label unless overridden.

### RET

Return from a subroutine.

- Syntax: `RET`
- Encoding: opcode only.
- Behavior: Pops the return address, releases the callee frame, resumes execution at the return address.

### HALT

Stop program execution.

- Syntax: `HALT` / `HALT OK` / `HALT BAD` / `HALT <n>`
- Encoding: opcode, 1 byte exit code.
- Behavior: `OK` = exit code 0. `BAD` = exit code 1. Numeric form uses the given value.

## Memory

### LOADVAR / STOREVAR

Access frame-local variable slots (relative to the current call frame).

- Syntax: `LOADVAR <dst>, <slot>` / `STOREVAR <src>, <slot>`
- Encoding: opcode, 1 byte register, 4-byte slot index.

### LOADVAR_REG / STOREVAR_REG

Frame-local access with register-indexed slot.

- Syntax: `LOADVAR_REG <dst>, <idx_reg>` / `STOREVAR_REG <src>, <idx_reg>`
- Encoding: opcode, 1 byte register, 1 byte index register.

### LOADGLOBAL / STOREGLOBAL

Access the global variable segment (allocated by `%globals`).

- Syntax: `LOADGLOBAL <dst>, <slot>` / `STOREGLOBAL <src>, <slot>`
- Encoding: opcode, 1 byte register, 4-byte slot index.
- Behavior: Global slots are shared across all call frames and persist for the lifetime of the program.

### LOADREF / STOREREF

Access a variable slot in the caller's frame (for reference parameters).

- Syntax: `LOADREF <dst>, <src_reg>` / `STOREREF <dst_reg>, <src>`
- Encoding: opcode, 1 byte dst, 1 byte src.
- Behavior: The slot index is taken from a register. Accesses the frame one level up the call stack.

### LOADSTR

Load a string handle into a register.

- Syntax: `LOADSTR $<name>, <reg>`
- Encoding: opcode, 1 byte register, 4-byte string index.
- Behavior: Loads the index of the named string from the string table into the register. Use `PRINTSTR` to print it.

### LOAD / STORE

Access the raw operand stack by absolute index.

- Syntax: `LOAD <reg>, <index>` / `STORE <reg>, <index>`
- Encoding: opcode, 1 byte register, 4-byte index.
- Behavior: Reads or writes a 64-bit slot in the operand stack by absolute position. Index must be within current stack
  capacity.

### LOAD_REG / STORE_REG

Operand stack access with register-indexed address.

- Syntax: `LOAD_REG <reg>, <idx_reg>` / `STORE_REG <reg>, <idx_reg>`
- Encoding: opcode, 1 byte register, 1 byte index register.

### ALLOC

Ensure operand stack capacity.

- Syntax: `ALLOC <n>`
- Encoding: opcode, 4-byte unsigned count.
- Behavior: If `n` exceeds current capacity, the stack is resized to exactly `n` slots. No-op otherwise.
- Privilege: PRIVILEGED only.

### GROW

Increase operand stack capacity by additional slots.

- Syntax: `GROW <n>`
- Encoding: opcode, 4-byte unsigned count.
- Behavior: Increases capacity by `n`. No-op if `n` is zero.
- Privilege: PRIVILEGED only.

### RESIZE

Set operand stack capacity to an exact size.

- Syntax: `RESIZE <n>`
- Encoding: opcode, 4-byte unsigned count.
- Behavior: Resizes the stack to exactly `n` slots.
- Privilege: PRIVILEGED only.

### FREE

Reduce operand stack capacity.

- Syntax: `FREE <n>`
- Encoding: opcode, 4-byte unsigned count.
- Behavior: Decreases capacity by `n`. Error if `n` exceeds current capacity.
- Privilege: PRIVILEGED only.

## File I/O

### FOPEN

Open a file into a file descriptor slot.

- Syntax: `FOPEN <mode>, F<fd>, "<filename>"`
- Encoding: opcode, 1 byte mode, 1 byte fd, 4-byte filename length, then filename bytes.
- Modes: `r` = read, `w` = write (truncate), `a` = append.
- Privilege: PRIVILEGED only.

### FCLOSE

Close an open file descriptor.

- Syntax: `FCLOSE F<fd>`
- Encoding: opcode, 1 byte fd.
- Privilege: PRIVILEGED only.

### FREAD

Read one byte from a file descriptor into a register.

- Syntax: `FREAD F<fd>, <reg>`
- Encoding: opcode, 1 byte fd, 1 byte register.
- Behavior: Reads one byte. Stores -1 on EOF.

### FWRITE

Write a register value as a byte to a file descriptor.

- Syntax: `FWRITE F<fd>, <reg>`
- Encoding (register form): opcode, 1 byte fd, 1 byte register.
- Encoding (immediate form): opcode, 1 byte fd, 4-byte immediate.

### FSEEK

Seek the file position of a file descriptor.

- Syntax: `FSEEK F<fd>, <reg>` / `FSEEK F<fd>, <imm>`
- Encoding (register): opcode, 1 byte fd, 1 byte register.
- Encoding (immediate): opcode, 1 byte fd, 4-byte signed offset.
- Behavior: Sets the file position from the beginning of the file.

## System

### EXEC

Execute a system command.

- Syntax: `EXEC "<command>", <reg>`
- Encoding: opcode, 1 byte dest register, 4-byte command length, then command bytes.
- Behavior: Runs the command via the system shell. Stores the exit code in the register.
- Privilege: PRIVILEGED only.

### SLEEP

Sleep for a number of milliseconds.

- Syntax: `SLEEP <ms>` / `SLEEP <reg>`
- Encoding (immediate): opcode, 4-byte unsigned milliseconds.
- Encoding (register): opcode, 1 byte register.

### RAND

Generate a random number.

- Syntax: `RAND <reg>` / `RAND <reg>, <min>, <max>`
- Encoding: opcode, 1 byte register, 8-byte signed min (i64), 8-byte signed max (i64).
- Behavior: Generates a random 64-bit integer. If min and max are provided, result is in [min, max].

### GETKEY

Non-blocking keyboard check.

- Syntax: `GETKEY <reg>`
- Encoding: opcode, 1 byte register.
- Behavior: If a key is available, stores its ASCII code in the register. Otherwise stores -1. Does not block.

### CLRSCR

Clear the console screen.

- Syntax: `CLRSCR`
- Encoding: opcode only.

### GETARGC

Get the number of command-line arguments.

- Syntax: `GETARGC <reg>`
- Encoding: opcode, 1 byte register.

### GETARG

Get a command-line argument as a string handle.

- Syntax: `GETARG <reg>, <index>`
- Encoding: opcode, 1 byte register, 4-byte unsigned index.
- Behavior: Stores a runtime string handle for the argument at the given index.

### GETENV

Get an environment variable as a string handle.

- Syntax: `GETENV <reg>, "<varname>"` (bare name without quotes is also accepted by the assembler)
- Encoding: opcode, 1 byte register, 4-byte name length, then name bytes.
- Behavior: Stores a runtime string handle for the variable's value. Raises a fault if the variable does not exist.

### CONTINUE

No operation.

- Syntax: `CONTINUE`
- Encoding: opcode only.

## Privilege

### REGSYSCALL

Register a syscall handler.

- Syntax: `REGSYSCALL <id>, <label>`
- Encoding: opcode, 1 byte id, 4-byte address.
- Behavior: Registers the label as the handler for syscall `id`. Up to 256 handlers (ids 0-255).
- Privilege: PRIVILEGED only.

### SYSCALL

Invoke a registered syscall handler from protected code.

- Syntax: `SYSCALL <id>`
- Encoding: opcode, 1 byte id.
- Behavior: Saves the return address, elevates to PRIVILEGED, jumps to the registered handler. The handler returns with
  `SYSRET`.
- Privilege: PROTECTED only.

### SYSRET

Return from a syscall handler.

- Syntax: `SYSRET`
- Encoding: opcode only.
- Behavior: Drops back to PROTECTED and resumes after the `SYSCALL` instruction.
- Privilege: PRIVILEGED only.

### DROPPRIV

Drop to PROTECTED mode permanently (until a syscall).

- Syntax: `DROPPRIV`
- Encoding: opcode only.
- Behavior: Switches from PRIVILEGED to PROTECTED. The only way back is through a registered syscall handler.
- Privilege: PRIVILEGED only.

### GETMODE

Get the current privilege mode.

- Syntax: `GETMODE <reg>`
- Encoding: opcode, 1 byte register.
- Behavior: Stores 1 in the register if PRIVILEGED, 0 if PROTECTED.

### SETPERM

Set read/write permissions on operand stack slots.

- Syntax: `SETPERM <start>, <count>, <priv_perms>/<prot_perms>`
- Encoding: opcode, 4-byte start, 4-byte count, 1 byte priv_read, 1 byte priv_write, 1 byte prot_read, 1 byte
  prot_write.
- Behavior: Sets permissions for `count` slots starting at `start`. Permissions are specified as `RW/RW` (
  privileged/protected), e.g. `RW/R` means privileged can read and write, protected can only read.
- Privilege: PRIVILEGED only.

### REGFAULT

Register a fault handler.

- Syntax: `REGFAULT <id>, <label>`
- Encoding: opcode, 1 byte id, 4-byte address.
- Behavior: When a fault with the given id occurs, the VM elevates to PRIVILEGED and jumps to the handler. Up to the
  number of defined fault types are supported.
- Privilege: PRIVILEGED only.

### FAULTRET

Return from a fault handler.

- Syntax: `FAULTRET`
- Encoding: opcode only.
- Behavior: Drops back to PROTECTED and resumes after the instruction that raised the fault.
- Privilege: PRIVILEGED only.

### GETFAULT

Get the id of the current fault being handled.

- Syntax: `GETFAULT <reg>`
- Encoding: opcode, 1 byte register.
- Behavior: Stores the current fault id in the register (`FaultType::Count` when no fault is active).

## Debug

### BREAK

Trigger a debugger breakpoint.

- Syntax: `BREAK`
- Encoding: opcode only.
- Behavior: If a debugger is attached, pauses execution and drops into the debug REPL. Otherwise no-op.

### DUMPREGS

Print all register values.

- Syntax: `DUMPREGS`
- Encoding: opcode only.
- Behavior: Prints all register values to stdout.
