# Blackbox documentation
### Info
Syntax is Intel assembly-like (DST, SRC).\
Labels start with a period (.) and end with a colon (:) (.label:)\
The stack size is 16384 32-bit integers, 64KB total.\
The stack acts like virtual memory.\
There are 33 registers from R0 to R32, each can store a 32-bit integer.
Register 8 (R8) is dedicated to CMP. 
### Calls/Opcodes:
| Instruction | Description                              | Operands                     | Notes                                           |
| ----------- | ---------------------------------------- | ---------------------------- | ----------------------------------------------- |
| `WRITE`     | Write a string to a stream               | `<stream number> "<string>"` | `stream number = 1` for stdout, `2` for stderr  |
| `NEWLINE`   | Prints a newline                         | None                         | Equivalent to `\n`                              |
| `PRINT`     | Print a single character                 | `<char>`                     | Example: `PRINT 'A'`                            |
| `PUSH`      | Push 32-bit integer or reg to the stack  | `<value or register>`        | Stack grows; check for overflow                 |
| `POP`       | Pop the top of the stack into a register | `<register>`                 | Stack shrinks; check for underflow              |
| `MOV`       | Move a value into a register             | `<dst>, <src>`               | `<src>` can be immediate or another register    |
| `ADD`       | Add two registers                        | `<dst>, <src>`               | `dst = dst + src`                               |
| `SUB`       | Subtract two registers                   | `<dst>, <src>`               | `dst = dst - src`                               |
| `MUL`       | Multiply two registers                   | `<dst>, <src>`               | `dst = dst * src`                               |
| `DIV`       | Divide two registers                     | `<dst>, <src>`               | `dst = dst / src`, division by zero is an error |
| `PRINT_REG` | Print integer value of a register        | `<register>`                 | Outputs decimal number                          |
| `JMP`       | Jump unconditionally                     | `<label>`                    | Sets PC to the label’s address                  |
| `JZ`        | Jump if register is zero                 | `<register>, <label>`        | Conditional branch                              |
| `JNZ`       | Jump if register is non-zero             | `<register>, <label>`        | Conditional branch                              |
| `HALT`      | Stop program execution                   | None                         | Ends the program                                |
| `INC`       | Increment register by one                | `<register>`                 | `<register> + 1`                                |
| `DEC`       | Decrement register by one                | `<register>`                 | `<register> - 1`                                |
| `CMP`       | Compare 2 registers                      | `<register>, <register>`     | Subtracts the registers, if bigger than 0, R8=1 |