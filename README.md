# Blackbox
Blackbox is an assembly-like language with an assembler and a bytecode interpreter. It uses Intel Assembly-like syntax. (register order is dst, src)
### Examples
Hello World in Blackbox:
```
WRITE 1 "Hello World" ;1 for stdout
NEWLINE
HALT
```
Arithmetic operations:
```
PUSH 20
POP R0
PUSH 10
POP R1
ADD R0, R1
PRINT_REG R0 ;Result: 30
NEWLINE
HALT
``` 
### Building
Run `make` in the directory of either the interpreter or the assembler. `make clean` to delete any residual files from `make`

