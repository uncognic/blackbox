# Blackbox
## Overview
Blackbox (the language) is a small, mid to high level language that compiles down to Blackbox bytecode (`.bcx`) and runs on the existing Blackbox interpreter (`bbx`). 
It is compiled using the `bbxc` frontend, the same as Blackbox assembly.

## unsafe asm
Blackbox supports inline assembly in `unsafe asm` blocks. This allows you to write raw Blackbox assembly code directly in your source files. The safety of this code cannot be guaranteed, as it allows you to directly reference registers which are usually abstracted in Blackbox.
Example:

```
fn main() {
    // This runs the command `echo Hello World!` in the host environment and prints the exit code
    unsafe asm {
        EXEC "echo Hello World!", R00
        WRITE STDOUT, "exit code: "
        PRINTREG R00
        NEWLINE
        HALT OK
    }
}

```
