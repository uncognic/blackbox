.PHONY: all clean compiler interpreter disassembler copy

CC = clang-19
CXX = clang++-19
CFLAGS ?= -Wall -Wextra -O2
CXXFLAGS ?= -Wall -Wextra -O2 -std=c++23

all: compiler interpreter disassembler copy

compiler:
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/compiler.cpp -o src/blackboxc/compiler.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/asm.cpp -o src/blackboxc/asm.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/basic.cpp -o src/blackboxc/basic.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/data.cpp -o src/data.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/tools.cpp -o src/blackboxc/tools.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/asm_util.cpp -o src/blackboxc/asm_util.o
	$(CXX) src/blackboxc/compiler.o src/blackboxc/asm.o src/blackboxc/basic.o src/blackboxc/tools.o src/data.o src/blackboxc/asm_util.o -o src/blackboxc/bbxc

interpreter:
	$(CXX) $(CXXFLAGS) -Isrc/blackbox -Isrc/blackboxc -c src/blackbox/blackbox.cpp -o src/blackbox/blackbox.o
	$(CXX) $(CXXFLAGS) -Isrc/blackbox -Isrc/blackboxc -c src/blackbox/debug.cpp -o src/blackbox/debug.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/data.cpp -o src/data.o
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
