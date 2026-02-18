use std::ffi::{c_char, c_int};
use std::fs::File;
use std::io::{self, Read, Write};

#[unsafe(no_mangle)]
pub extern "C" fn compile(input: *const c_char, output: *const c_char, debug: c_int,) -> c_int {
    if (debug != 0) {
        println!("Debug mode enabled");
        println!("[DEBUG] In Rust");
    }
    
    let input_str = unsafe { std::ffi::CStr::from_ptr(input) }
        .to_str()
        .expect("Failed to convert input to string");

    let output_str = unsafe { std::ffi::CStr::from_ptr(output) }
        .to_str()
        .expect("Failed to convert output to string");

    if (debug != 0) {
        println!("[DEBUG] Input: {}", input_str);
        println!("[DEBUG] Output: {}", output_str);
    }

    let mut input = match File::open(input_str) {
        Ok(file) => file,
        Err(e) => {
            eprintln!("Failed to open input file: {}", e);
            return 1;
        }
    };

    let mut output = match File::create(output_str) {
        Ok(file) => file,
        Err(e) => {
            eprintln!("Failed to create output file: {}", e);
            return 1;
        }
    };
    0
}