CC = gcc
CFLAGS = -Wall -Wextra -O2

SRC = blackbox.c ../assembler/tools.c
HEADER = ../define.h ../assembler/tools.h
TARGET = bbx

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) $(HEADER)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)