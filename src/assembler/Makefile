CC = gcc
CFLAGS = -Wall -Wextra -O2

SRC = assembler.c
HEADER = ../interpreter/opcodes.h
TARGET = bbx-asm

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) $(HEADER)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)