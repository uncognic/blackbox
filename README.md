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
1. Ensure you have atleast Clang and make installed. On Ubuntu 24.04, you might have to manually install newer versions of Clang:
Ubuntu:
```bash
sudo apt install clang-19 libc++-19-dev libc++abi-19-dev
```
2. Run `make` in the project directory

### Windows MSVC
1. Ensure you have atleast Visual Studio 2022 with the "Desktop development with C++" workload and "C++ Clang compiler for Windows" component installed.
2. Ensure you have the Rust programming language installed.
3. Open the Developer Command Prompt for Visual Studio.
4. Run `build.bat` in the project directory.

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
