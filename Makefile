.PHONY: all clean copy compiler assembler interpreter rust disassembler

all: rust compiler interpreter disassembler copy

compiler:
	$(MAKE) -C src/assembler

assembler: compiler

interpreter:
	$(MAKE) -C src/interpreter

rust:
	cargo build --release --manifest-path=src/bbxc-blackbox/Cargo.toml

disassembler:
	cargo build --release --manifest-path=src/bbx-disasm/Cargo.toml

copy:
	cp src/interpreter/bbx .
	cp src/assembler/bbxc .
	cp target/release/bbxd .

clean:
	$(MAKE) -C src/interpreter clean || true
	$(MAKE) -C src/assembler clean || true
	cargo clean --manifest-path=src/bbxc-blackbox/Cargo.toml || true
	cargo clean --manifest-path=src/bbx-disasm/Cargo.toml || true
	rm -f bbx bbxc bbxd
