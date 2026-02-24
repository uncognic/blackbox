CC = gcc
CFLAGS = -Wall -Wextra -O2

SRC = blackbox.c debug.c ../tools.c
HEADER = ../define.h ../tools.h debug.h
TARGET = bbx

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) $(HEADER)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)