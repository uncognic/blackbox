```
 ____  _            _    _
| __ )| | __ _  ___| | _| |__   _____  __
|  _ \| |/ _` |/ __| |/ / '_ \ / _ \ \/ /
| |_) | | (_| | (__|   <| |_) | (_) >  <
|____/|_|\__,_|\___|_|\_\_.__/ \___/_/\_\
```

A minimal, Turing-complete bytecode virtual machine written in C, featuring a custom assembly language and instruction set.

The virtual machine executes a custom bytecode format produced from a simple assembly-like language.

It uses Intel-style syntax. (register order dst, src), (.label:)

This project explores low-level concepts such as instruction decoding, registers, stacks, and control flow, similar to how real CPUs operate.

### Docs
See [docs.md](docs/docs.md) and [examples.md](docs/examples.md)

### Building
Run `make` in the project directory on a Linux or macOS host.
