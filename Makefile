.PHONY: all clean compiler interpreter disassembler copy

CC = clang
CFLAGS ?= -Wall -Wextra -O2

all: compiler interpreter disassembler copy

compiler:
	$(CC) $(CFLAGS) src/blackboxc/compiler.c src/blackboxc/asm.c src/blackboxc/tools.c src/blackboxc/basic.c src/data.c -Isrc/blackboxc -o src/blackboxc/bbxc

interpreter:
	$(CC) $(CFLAGS) src/blackbox/blackbox.c src/blackbox/debug.c src/blackboxc/tools.c src/data.c -Isrc/blackbox -Isrc/blackboxc -o src/blackbox/bbx

disassembler:
	cargo build --release --manifest-path=src/bbx-disasm/Cargo.toml

copy:
	cp src/blackbox/bbx .
	cp src/blackboxc/bbxc .
	cp target/release/bbxd .

clean:
	rm -f bbx bbxc bbxd
	rm -f src/blackbox/bbx src/blackboxc/bbxc
	cargo clean --manifest-path=src/bbx-disasm/Cargo.toml || true
