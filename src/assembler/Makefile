CC = gcc
CFLAGS = -Wall -Wextra -O2
INCLUDES = -I.

SRC = ../compiler.c ../tools.c asm.c
OBJ = $(SRC:.c=.o)
HEADER = ../define.h ../tools.h asm.h
TARGET = bbxc

RUST_MANIFEST = ../source/Cargo.toml
RUST_LIB = ../source/target/release/libbbx_source.a

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ) $(RUST_LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJ) $(RUST_LIB) -o $(TARGET)

%.o: %.c $(HEADER)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)