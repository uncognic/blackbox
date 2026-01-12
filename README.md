```
 ____  _            _    _
| __ )| | __ _  ___| | _| |__   _____  __
|  _ \| |/ _` |/ __| |/ / '_ \ / _ \ \/ /
| |_) | | (_| | (__|   <| |_) | (_) >  <
|____/|_|\__,_|\___|_|\_\_.__/ \___/_/\_\
```
# Overview
A minimal, Turing-complete bytecode virtual machine written in C, featuring a custom assembly language and instruction set.

The virtual machine executes a custom bytecode format produced from a simple assembly-like language.
# Features
- Small, portable VM written in C with a compact custom bytecode format.
- 99 general-purpose registers (R0–R98).
- Heap-backed int64_t stack with ALLOC/GROW instructions.
- Straightforward Intel-like assembly and label-based control flow.
- Minimal, easy-to-read codebase. Great for learning, embedding, or extending with new opcodes.
- Deterministic, compact bytecode (small binaries, predictable behavior) Great for tests and reproducible demos.
# Docs
See [docs.md](docs/docs.md) and [examples.md](docs/examples.md)
# Building
### Unix-like (Linux, macOS, BSD family)
1. Ensure you have `gcc` and `make` installed.
2. Run `make` in the project directory.
### Windows
1. Ensure you have atleast Visual Studio  2022 with the "Desktop development with C++" workload installed.
2. Open the Developer Command Prompt for Visual Studio.
3. Run `build.bat` in the project directory.
