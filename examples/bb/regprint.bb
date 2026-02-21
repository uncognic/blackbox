fn main() {
    unsafe {
        MOV(R0, 42);
        PRINTREG(R0);
        NEWLINE();
        HALT();
    }
}
