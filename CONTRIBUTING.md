# Adding an opcode

1. Define the opcode in `src/define.h`
2. Implement the opcode in `src/blackboxc/asm.c` (for parsing assembly)
3. Implement the opcode in `src/blackbox/blackbox.c` (for execution)
4. Add the opcode in `src/blackboxc/tools.c` for size calculation
5. Add the opcode in `src/blackboxc/debug.c` for debugging
6. Add the opcode in `src/disassembler/src/main.rs` for disassembly
7. Document the opcode in `docs/assembly/ISA.md`
8. Add an example in `examples/assembly/`
9. If the opcode is relevant to BASIC, add support for it in `src/blackboxc/basic.c` and document it in `docs/basic/DOCS.md` and `docs/basic/SYNTAX.md`, and add an example in `examples/basic/`

