# Blackbox
A minimal, Turing-complete bytecode virtual machine written in C, featuring a custom assembly language and instruction set.

The virtual machine executes a custom bytecode format produced from a simple assembly-like language.

It uses Intel-style syntax. (register order dst, src), (.label:)

### Docs
See [docs.md](docs/docs.md) and [examples.md](docs/examples.md)

### Building
Run `make` in the project directory on a Linux or macOS host.
