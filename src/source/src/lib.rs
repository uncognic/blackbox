mod ast;
mod codegen;
mod lexer;
mod parser;

use std::ffi::{c_char, c_int};
use std::fs::File;
use std::io::Read;

use crate::codegen::emit_program;
use crate::lexer::Lexer;
use crate::parser::Parser;

#[unsafe(no_mangle)]
pub extern "C" fn compile(input: *const c_char, output: *const c_char, debug: c_int) -> c_int {
    let input_str = unsafe { std::ffi::CStr::from_ptr(input) }
        .to_str()
        .expect("Failed to convert input to string");

    let output_str = unsafe { std::ffi::CStr::from_ptr(output) }
        .to_str()
        .expect("Failed to convert output to string");

    let mut src = String::new();
    match File::open(input_str).and_then(|mut f| f.read_to_string(&mut src)) {
        Ok(_) => {}
        Err(e) => {
            eprintln!("Failed to read input file: {}", e);
            return 1;
        }
    }

    let lexer = Lexer::new(&src);
    let mut parser = Parser::new(lexer);
    match parser.parse_program() {
        Ok(program) => {
            if debug != 0 {
                println!("[DEBUG] Parsed AST: {:#?}", program);
            }
            match emit_program(&program) {
                Ok(bytes) => {
                    if let Err(e) = std::fs::write(output_str, &bytes) {
                        eprintln!("Failed to write output file: {}", e);
                        1
                    } else {
                        0
                    }
                }
                Err(e) => {
                    eprintln!("Codegen error: {}", e);
                    1
                }
            }
        }
        Err(err) => {
            eprintln!("Parse error: {}", err);
            1
        }
    }
}
