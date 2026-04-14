```
 ____  _            _    _
| __ )| | __ _  ___| | _| |__   _____  __
|  _ \| |/ _` |/ __| |/ / '_ \ / _ \ \/ /
| |_) | | (_| | (__|   <| |_) | (_) >  <
|____/|_|\__,_|\___|_|\_\_.__/ \___/_/\_\
```
A small, Turing-complete bytecode virtual machine with an assembly and BASIC-like language. It also features a disassembler/decompiler and debugger.

## Links
- Docs (assembly): [docs/asm/DOCS.md](docs/asm/DOCS.md)
- Docs (BASIC): [docs/basic/DOCS.md](docs/basic/DOCS.md)
- ISA reference (assembly): [docs/asm/ISA.md](docs/asm/ISA.md)  
- BASIC syntax reference: [docs/basic/SYNTAX.md](docs/basic/SYNTAX.md)
- Debugger [docs/asm/DEBUGGER.md](docs/asm/DEBUGGER.md)
- Examples: [examples/](examples/)
- License: [LICENSE](LICENSE)

## Build
### Unix-like:
1. Ensure you have GCC and CMake installed. On Ubuntu 24.04:
```bash
sudo apt install g++-14 cmake
```
2. Run the following commands in the project directory:
```bash
cmake -B build
cmake --build build
```

### Windows
#### Visual Studio 2022
1. Ensure you have Visual Studio 2022 with the "Desktop development with C++" workload installed.
2. Open the repo folder in Visual Studio
3. Visual Studio will detect CMakeLists.txt and configure automatically.
4. Build with Build All (Ctrl+Shift+B).

#### Command Line (clang-cl)
1. Ensure you have Visual Studio 2022 with the "Desktop development with C++" workload installed.
2. Open the Developer Command Prompt for Visual Studio.
3. Run the following in the project directory:
```bat
cmake -B build
cmake --build build
```

## Examples

Assemble and run the Hello World example in Assembly:
```sh
./bbxc examples/helloworld.bbx hello.bcx
./bbx hello.bcx
```
Compile and run the Hello World example in BASIC:
```sh
./bbxc examples/basic/helloworld.bbs hello.bcx
./bbx hello.bcx
```

Or compile an arbitrary program (there are many examples)
```sh
./bbxc path/to/program.bbx program.bcx
./bbx program.bcx
```
## License
This project is Free Software under the [GPLv3](LICENSE) license.
