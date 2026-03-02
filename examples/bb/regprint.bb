fn main() {
    unsafe {
        mov(R0, 42);
        printreg(R0);
        newline();
        halt();
    }
}
