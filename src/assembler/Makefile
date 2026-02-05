CC = gcc
CFLAGS = -Wall -Wextra -O2
INCLUDES = -I.

SRC = assembler.c tools.c asm.c
OBJ = $(SRC:.c=.o)
HEADER = ../define.h tools.h asm.h
TARGET = bbx-asm

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJ) -o $(TARGET)

%.o: %.c $(HEADER)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)