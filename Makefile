.PHONY: all clean copy

all:
	$(MAKE) -C src/interpreter
	$(MAKE) -C src/assembler
	$(MAKE) copy

copy:
	cp src/interpreter/bbx .
	cp src/assembler/bbxc .

clean:
	$(MAKE) -C src/interpreter clean
	$(MAKE) -C src/assembler clean
	rm -f bbx bbxc
