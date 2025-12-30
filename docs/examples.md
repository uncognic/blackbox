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
Unconditional JMP:
```
.label1:
MOV R1, 1
PRINT_REG R1
NEWLINE
JMP label2

.label2:
MOV R2, 2
PRINT_REG R2
NEWLINE
JMP label3

.label3:
MOV R3, 3
PRINT_REG R3
NEWLINE
JMP label1
HALT
```
Conditional JZ and JNZ:
```
MOV R1, 3

.label_loop:
PRINT_REG R1
NEWLINE

MOV R3, 1
SUB R1, R3
JNZ R1, label_loop

MOV R4, 42
JZ R1, label_end
PRINT_REG R4 ;SHOULD NOT PRINT 42!

.label_end:
HALT
```
Prime Sieve: 
```
MOV R0, 50
MOV R1, 2 
.loop_outer:
    MOV R2, 2
    MOV R3, 1

.loop_inner:
    MOV R4, R1
    DIV R4, R2
    MUL R4, R2
    SUB R4, R1
    JZ R4, not_prime
    INC R2
    MOV R5, R2
    SUB R5, R1
    JNZ R5, loop_inner

    PRINT_REG R1
    NEWLINE
    JMP next_number

.not_prime:


.next_number:
    INC R1
    MOV R5, R1
    SUB R5, R0
    JNZ R5, loop_outer

HALT
```
