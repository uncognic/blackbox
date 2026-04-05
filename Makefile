.PHONY: all clean compiler interpreter disassembler copy

CC = clang
CXX = clang++
CFLAGS ?= -Wall -Wextra -O2
CXXFLAGS ?= -Wall -Wextra -O2 -std=c++23

all: compiler interpreter disassembler copy

compiler:
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/compiler.cpp -o src/blackboxc/compiler.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/asm.cpp -o src/blackboxc/asm.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/basic.cpp -o src/blackboxc/basic.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/data.cpp -o src/data.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/tools.cpp -o src/tools.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/asm_util.cpp -o src/blackboxc/asm_util.o
	$(CXX) src/blackboxc/compiler.o src/blackboxc/asm.o src/blackboxc/basic.o src/tools.o src/data.o src/blackboxc/asm_util.o -o src/blackboxc/bbxc

interpreter:
	$(CXX) $(CXXFLAGS) -Isrc/blackbox -Isrc/blackboxc -c src/blackbox/blackbox.cpp -o src/blackbox/blackbox.o
	$(CXX) $(CXXFLAGS) -Isrc/blackbox -Isrc/blackboxc -c src/blackbox/debug.cpp -o src/blackbox/debug.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/data.cpp -o src/data.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/tools.cpp -o src/tools.o
	$(CXX) $(CXXFLAGS) -Isrc/blackbox -c src/fmt.cpp -o src/fmt.o
	$(CXX) src/blackbox/blackbox.o src/blackbox/debug.o src/tools.o src/data.o src/fmt.o -o src/blackbox/bbx

disassembler:
	$(CXX) $(CXXFLAGS) -Isrc -c src/blackboxd/blackboxd.cpp -o src/blackboxd/blackboxd.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/data.cpp -o src/data.o
	$(CXX) src/blackboxd/blackboxd.o src/data.o -o src/blackboxd/bbxd

copy:
	cp src/blackbox/bbx .
	cp src/blackboxc/bbxc .
	cp src/blackboxd/bbxd .

clean:
	rm -f bbx bbxc bbxd
	rm -f src/blackbox/bbx src/blackboxc/bbxc src/blackboxd/bbxd
	rm -f src/blackbox/*.o src/blackboxc/*.o src/*.o
	rm -f src/blackboxd/*.o
