Hello World in Blackbox:
```
WRITE 1 "Hello World" ;1 for stdout
NEWLINE
HALT
```
Arithmetic operations:
```
;Add operation
PUSH 20
POP R0
PUSH 10
POP R1
ADD R0, R1
PRINT_REG R0 ;Result: 30

;Subtract operation
NEWLINE
PUSH 2
POP R2
SUB R0, R2
PRINT_REG R0 ;Result: 28

;Multiply operation
NEWLINE
PUSH 2
POP R2
MUL R0, R2
PRINT_REG R0 ;Result: 56

;Divide operation
NEWLINE
PUSH 2
POP R2
DIV R0, R2
PRINT_REG R0 ;Result: 28

NEWLINE
HALT
``` 
MOV operation:
```
MOV R0, 42
MOV R1, R0
PRINT_REG R1
NEWLINE
HALT
```
