#pragma once

#include <stdint.h>

#define MAGIC 0x626378
#define MAGIC_SIZE 3
#define HEADER_FIXED_SIZE 8
#define STACK_SIZE 16384
#define REGISTERS 99
#define FILE_DESCRIPTORS 99
#define VAR_CAPACITY 16384
#define MAX_SYSCALLS 256

typedef enum
{
    FAULT_PERM_READ = 0,
    FAULT_PERM_WRITE = 1,
    FAULT_BAD_SYSCALL = 2,
    FAULT_PRIV = 3,
    FAULT_DIV_ZERO = 4,
    FAULT_OOB = 5,
    FAULT_COUNT = 6  
} Fault;

typedef struct
{
    uint8_t priv_read;
    uint8_t priv_write;
    uint8_t prot_read;
    uint8_t prot_write;
} SlotPermission;

typedef enum
{
    MODE_PROTECTED,
    MODE_PRIVILEGED
} Mode;

typedef enum
{
    DATA_STRING,
    DATA_DWORD,
    DATA_QWORD,
    DATA_WORD,
    DATA_BYTE
} DataType;

typedef struct
{
    char name[32];
    DataType type;
    char *str;
    uint8_t byte;
    uint16_t word;
    uint32_t dword;
    uint64_t qword;
    uint32_t offset;
} Data;

typedef struct
{
    char *name;
    char **params;
    int paramc;
    char **body;
    int bodyc;
} Macro;

typedef struct
{
    char name[32];
    uint32_t addr;
    uint32_t frame_size;
} Label;

#define OPCODE_WRITE 0x01
#define OPCODE_NEWLINE 0x02
#define OPCODE_PRINT 0x03
#define OPCODE_PUSHI 0x04
#define OPCODE_POP 0x05
#define OPCODE_ADD 0x06
#define OPCODE_SUB 0x07
#define OPCODE_MUL 0x08
#define OPCODE_DIV 0x09
#define OPCODE_PRINTREG 0x0A
#define OPCODE_MOVI 0x0B
#define OPCODE_MOV_REG 0x0C
#define OPCODE_JMP 0x0D
#define OPCODE_JE 0x0E
#define OPCODE_JNE 0x0F
#define OPCODE_INC 0x10
#define OPCODE_DEC 0x11
#define OPCODE_PUSH_REG 0x12
#define OPCODE_CMP 0x13
#define OPCODE_ALLOC 0x14
#define OPCODE_LOAD 0x15
#define OPCODE_STORE 0x16
#define OPCODE_LOAD_REG 0x41
#define OPCODE_STORE_REG 0x42
#define OPCODE_LOADVAR 0x43
#define OPCODE_STOREVAR 0x44
#define OPCODE_LOADVAR_REG 0x45
#define OPCODE_STOREVAR_REG 0x46
#define OPCODE_GROW 0x17
#define OPCODE_PRINT_STACKSIZE 0x18
#define OPCODE_RESIZE 0x19
#define OPCODE_FREE 0x20
#define OPCODE_FOPEN 0x21
#define OPCODE_FCLOSE 0x22
#define OPCODE_FREAD 0x23
#define OPCODE_FWRITE_REG 0x24
#define OPCODE_FWRITE_IMM 0x27
#define OPCODE_FSEEK_REG 0x25
#define OPCODE_FSEEK_IMM 0x26
#define OPCODE_LOADSTR 0x28
#define OPCODE_PRINTSTR 0x29
#define OPCODE_XOR 0x2A
#define OPCODE_AND 0x2B
#define OPCODE_OR 0x2C
#define OPCODE_NOT 0x2D
#define OPCODE_READSTR 0x2E
#define OPCODE_READ 0x33
#define OPCODE_SLEEP 0x2F
#define OPCODE_CLRSCR 0x30
#define OPCODE_RAND 0x31
#define OPCODE_GETKEY 0x32
#define OPCODE_CONTINUE 0x34
#define OPCODE_READCHAR 0x35
#define OPCODE_JL 0x36
#define OPCODE_JGE 0x37
#define OPCODE_JB 0x38
#define OPCODE_JAE 0x39
#define OPCODE_CALL 0x3A
#define OPCODE_RET 0x3B
#define OPCODE_LOADBYTE 0x3C
#define OPCODE_LOADWORD 0x3D
#define OPCODE_LOADDWORD 0x3E
#define OPCODE_LOADQWORD 0x3F
#define OPCODE_MOD 0x40
#define OPCODE_JMPI 0x47
#define OPCODE_EXEC 0x48
#define OPCODE_SYSCALL 0x50
#define OPCODE_SYSRET 0x51
#define OPCODE_DROPPRIV 0x52
#define OPCODE_REGSYSCALL 0x53
#define OPCODE_SETPERM 0x54
#define OPCODE_GETMODE 0x55
#define OPCODE_REGFAULT 0x56
#define OPCODE_FAULTRET 0x57
#define OPCODE_GETFAULT 0x58
#define OPCODE_DUMPREGS 0x59
#define OPCODE_PRINTCHAR 0x5A
#define OPCODE_HALT 0xFF
#define OPCODE_BREAK 0xFE
