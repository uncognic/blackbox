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
