# Adding an opcode

1. Define the opcode in `src/define.h`
2. Implement the opcode in `src/assembler/asm.c` (for parsing assembly)
3. Implement the opcode in `src/interpreter/blackbox.c` (for execution)
4. Add the opcode in `src/tools.c` for size calculation
5. Add the opcode in `src/interpreter/debug.c` for debugging
6. Add the opcode in `src/disassembler/src/main.rs` for disassembly
7. Document the opcode in `docs/assembly/ISA.md`
8. Add an `InstrDef` entry in `src/bbxc-asm/src/lib.rs` for inline assembly support for Blackbox the language
9. (Optional) Add an example in `examples/assembly/`
