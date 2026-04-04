.PHONY: all clean compiler interpreter disassembler copy

CC = clang
CXX = clang++
CFLAGS ?= -Wall -Wextra -O2
CXXFLAGS ?= -Wall -Wextra -O2

all: compiler interpreter disassembler copy

compiler:
	$(CC) $(CFLAGS) -Isrc/blackboxc -c src/blackboxc/compiler.c -o src/blackboxc/compiler.o
	$(CC) $(CFLAGS) -Isrc/blackboxc -c src/blackboxc/asm.c -o src/blackboxc/asm.o
	$(CC) $(CFLAGS) -Isrc/blackboxc -c src/blackboxc/basic.c -o src/blackboxc/basic.o
	$(CC) $(CFLAGS) -Isrc/blackboxc -c src/data.c -o src/data.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/tools.cpp -o src/blackboxc/tools.o
	$(CXX) src/blackboxc/compiler.o src/blackboxc/asm.o src/blackboxc/basic.o src/blackboxc/tools.o src/data.o -o src/blackboxc/bbxc

interpreter:
	$(CC) $(CFLAGS) -Isrc/blackbox -Isrc/blackboxc -c src/blackbox/blackbox.c -o src/blackbox/blackbox.o
	$(CC) $(CFLAGS) -Isrc/blackbox -Isrc/blackboxc -c src/blackbox/debug.c -o src/blackbox/debug.o
	$(CC) $(CFLAGS) -Isrc/blackboxc -c src/data.c -o src/data.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/tools.cpp -o src/blackboxc/tools.o
	$(CXX) src/blackbox/blackbox.o src/blackbox/debug.o src/blackboxc/tools.o src/data.o -o src/blackbox/bbx

disassembler:
	cargo build --release --manifest-path=src/bbx-disasm/Cargo.toml

copy:
	cp src/blackbox/bbx .
	cp src/blackboxc/bbxc .
	cp target/release/bbxd .

clean:
	rm -f bbx bbxc bbxd
	rm -f src/blackbox/bbx src/blackboxc/bbxc
	rm -f src/blackbox/*.o src/blackboxc/*.o src/*.o
	cargo clean --manifest-path=src/bbx-disasm/Cargo.toml || true
