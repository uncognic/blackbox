# BBLang
## Overview
bblang is a small, high-level language that compiles down to Blackbox bytecode (`.bcx`) and runs on the existing Blackbox interpreter (`bbx`). 
It is compiled using `bbxc`, the same as Blackbox assembly.

## Unsafe
Any code that directly references VM registers (e.g. `R0`, `R1`) must be placed inside an `unsafe` block.
Example:

```
fn main() {
    unsafe {
        MOV(R0, 42);
        PRINTREG(R0);
        NEWLINE();
        HALT();
    }
}
```
