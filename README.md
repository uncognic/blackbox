```
 ____  _            _    _
| __ )| | __ _  ___| | _| |__   _____  __
|  _ \| |/ _` |/ __| |/ / '_ \ / _ \ \/ /
| |_) | | (_| | (__|   <| |_) | (_) >  <
|____/|_|\__,_|\___|_|\_\_.__/ \___/_/\_\
```
A small, Turing-complete bytecode virtual machine with a simple assembly language and assembler/interpreter.

## Links
- Docs: [docs/docs.md](docs/docs.md)
- ISA reference: [docs/isa.md](docs/isa.md)  
- Examples: [examples/](examples/)  
- License: [LICENSE](LICENSE)

## Build
### Unix-like:
1. Ensure you have `gcc` and `make` installed
2. Run `make` in the project directory
### Windows MSVC
1. Ensure you have atleast Visual Studio  2022 with the "Desktop development with C++" workload installed. 
2. Open the Developer Command Prompt for Visual Studio.
3. Run `build.bat` in the project directory.

## Examples

Assemble and run the Hello World example:
```sh
./bbxc examples/helloworld.bbx hello.bcx
./bbx hello.bcx
```

Or compile an arbitrary program (there are many examples)
```sh
./bbxc path/to/program.bbx program.bcx
./bbx program.bcx
```
## License
This project is Free Software under the [GPLv3](LICENSE) license.
