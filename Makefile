.PHONY: all clean copy compiler assembler interpreter rust

all: rust compiler interpreter copy

compiler:
	$(MAKE) -C src/assembler

assembler: compiler

interpreter:
	$(MAKE) -C src/interpreter

rust:
	cargo build --release --manifest-path=src/source/Cargo.toml

copy:
	cp src/interpreter/bbx .
	cp src/assembler/bbxc .

clean:
	$(MAKE) -C src/interpreter clean || true
	$(MAKE) -C src/assembler clean || true
	cargo clean --manifest-path=src/source/Cargo.toml || true
	rm -f bbx bbxc
