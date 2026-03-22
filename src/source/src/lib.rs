mod ast;
mod codegen;
mod lexer;
mod parser;

use std::ffi::{c_char, c_int};
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::ptr;

use crate::codegen::emit_program;
use crate::lexer::Lexer;
use crate::parser::Parser;

const MAX_INCLUDE_DEPTH: usize = 32;

fn parse_include_directive(line: &str) -> Result<Option<String>, String> {
    if line.len() < 8 || !line[..8].eq_ignore_ascii_case("%include") {
        return Ok(None);
    }

    let mut rest = &line[8..];
    if let Some(ch) = rest.chars().next() {
        if ch != ' ' && ch != '\t' {
            return Ok(None);
        }
    }

    rest = rest.trim_start_matches([' ', '\t']);
    if !rest.starts_with('"') {
        return Err("expected %include \"file.bbx\"".to_string());
    }

    let rest = &rest[1..];
    let Some(end_quote) = rest.find('"') else {
        return Err("expected %include \"file.bbx\"".to_string());
    };

    if end_quote == 0 {
        return Err("expected %include \"file.bbx\"".to_string());
    }

    let include_path = &rest[..end_quote];
    let trailing = rest[end_quote + 1..].trim();
    if !trailing.is_empty() {
        return Err("expected %include \"file.bbx\"".to_string());
    }

    Ok(Some(include_path.to_string()))
}

fn collect_source(path: &Path, depth: usize, out: &mut String) -> Result<(), String> {
    if depth > MAX_INCLUDE_DEPTH {
        return Err(format!(
            "Error: include depth exceeded ({}) while reading {}",
            MAX_INCLUDE_DEPTH,
            path.display()
        ));
    }

    let src = std::fs::read_to_string(path)
        .map_err(|_| format!("Error: cannot open input file '{}'", path.display()))?;

    let base_dir: PathBuf = path
        .parent()
        .map_or_else(|| PathBuf::from("."), PathBuf::from);

    for (lineno, raw_line) in src.split_inclusive('\n').enumerate() {
        let mut trimmed = raw_line.trim();
        if let Some(idx) = trimmed.find(';') {
            trimmed = trimmed[..idx].trim();
        }

        match parse_include_directive(trimmed) {
            Ok(Some(include_rel)) => {
                let include_path = Path::new(&include_rel);
                let resolved = if include_path.is_absolute() {
                    include_path.to_path_buf()
                } else {
                    base_dir.join(include_path)
                };
                collect_source(&resolved, depth + 1, out)?;
            }
            Ok(None) => out.push_str(raw_line),
            Err(msg) => {
                return Err(format!(
                    "Syntax error in {}:{}: {}",
                    path.display(),
                    lineno + 1,
                    msg
                ));
            }
        }
    }

    Ok(())
}

#[unsafe(no_mangle)]
pub extern "C" fn preprocess_includes(input: *const c_char) -> *mut c_char {
    if input.is_null() {
        eprintln!("Error: null input path");
        return ptr::null_mut();
    }

    let input_path = match unsafe { std::ffi::CStr::from_ptr(input) }.to_str() {
        Ok(s) => s,
        Err(_) => {
            eprintln!("Error: invalid input path");
            return ptr::null_mut();
        }
    };

    let mut out = String::new();
    if let Err(err) = collect_source(Path::new(input_path), 0, &mut out) {
        eprintln!("{}", err);
        return ptr::null_mut();
    }

    match std::ffi::CString::new(out) {
        Ok(c) => c.into_raw(),
        Err(_) => {
            eprintln!("Error: preprocessed source contains NUL byte");
            ptr::null_mut()
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn preprocess_includes_free(buf: *mut c_char) {
    if buf.is_null() {
        return;
    }
    unsafe {
        let _ = std::ffi::CString::from_raw(buf);
    }
}

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
