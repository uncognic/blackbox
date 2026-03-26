fn main() {
    unsafe asm {
        EXEC "echo Hello World!", R00
        WRITE STDOUT, "exit code: "
        PRINTREG R00
        NEWLINE
        HALT OK
    }
}
