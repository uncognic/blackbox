.PHONY: all clean compiler interpreter disassembler copy

CC = clang
CXX = clang++
CFLAGS ?= -Wall -Wextra -O2
CXXFLAGS ?= -Wall -Wextra -O2 -std=c++23 -stdlib=libc++

all: compiler interpreter disassembler copy

compiler:
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/compiler.cpp -o src/blackboxc/compiler.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/asm.cpp -o src/blackboxc/asm.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/basic.cpp -o src/blackboxc/basic.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/utils/data.cpp -o src/data.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/string_utils.cpp -o src/utils/string_utils.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/asm_parser.cpp -o src/utils/asm_parser.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/preprocessor.cpp -o src/utils/preprocessor.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/macro_expansion.cpp -o src/utils/macro_expansion.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/symbol_table.cpp -o src/utils/symbol_table.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/random_utils.cpp -o src/utils/random_utils.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/blackboxc/asm_util.cpp -o src/blackboxc/asm_util.o
	$(CXX) $(CXXFLAGS) src/blackboxc/compiler.o src/blackboxc/asm.o src/blackboxc/basic.o src/data.o src/utils/string_utils.o src/utils/asm_parser.o src/utils/preprocessor.o src/utils/macro_expansion.o src/utils/symbol_table.o src/utils/random_utils.o src/blackboxc/asm_util.o -o src/blackboxc/bbxc

interpreter:
	$(CXX) $(CXXFLAGS) -Isrc/blackbox -Isrc/blackboxc -c src/blackbox/blackbox.cpp -o src/blackbox/blackbox.o
	$(CXX) $(CXXFLAGS) -Isrc/blackbox -Isrc/blackboxc -c src/blackbox/debug.cpp -o src/blackbox/debug.o
	$(CXX) $(CXXFLAGS) -Isrc/blackboxc -c src/utils/data.cpp -o src/data.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/string_utils.cpp -o src/utils/string_utils.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/asm_parser.cpp -o src/utils/asm_parser.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/preprocessor.cpp -o src/utils/preprocessor.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/macro_expansion.cpp -o src/utils/macro_expansion.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/symbol_table.cpp -o src/utils/symbol_table.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/random_utils.cpp -o src/utils/random_utils.o
	$(CXX) $(CXXFLAGS) -Isrc/blackbox -c src/utils/fmt.cpp -o src/fmt.o
	$(CXX) $(CXXFLAGS) src/blackbox/blackbox.o src/blackbox/debug.o src/data.o src/utils/string_utils.o src/utils/asm_parser.o src/utils/preprocessor.o src/utils/macro_expansion.o src/utils/symbol_table.o src/utils/random_utils.o src/fmt.o -o src/blackbox/bbx

disassembler:
	$(CXX) $(CXXFLAGS) -Isrc -c src/blackboxd/blackboxd.cpp -o src/blackboxd/blackboxd.o
	$(CXX) $(CXXFLAGS) -Isrc -c src/utils/data.cpp -o src/data.o
	$(CXX) $(CXXFLAGS) src/blackboxd/blackboxd.o src/data.o -o src/blackboxd/bbxd

copy:
	cp src/blackbox/bbx .
	cp src/blackboxc/bbxc .
	cp src/blackboxd/bbxd .

clean:
	rm -f bbx bbxc bbxd
	rm -f src/blackbox/bbx src/blackboxc/bbxc src/blackboxd/bbxd
	rm -f src/blackbox/*.o src/blackboxc/*.o src/*.o
	rm -f src/blackboxd/*.o src/utils/*.o
