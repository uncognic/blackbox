use crate::ast::{Program, Statement};
use bbxc_asm::emit_asm_line;

include!(concat!(env!("OUT_DIR"), "/opcodes.rs"));

pub fn emit_program(prog: &Program) -> Result<Vec<u8>, String> {
    let mut code: Vec<u8> = Vec::new();

    let main_fn = prog
        .functions
        .iter()
        .find(|f| f.name == "main")
        .ok_or("missing main")?;

    let mut main_code: Vec<u8> = Vec::new();
    for stmt in &main_fn.body {
        match stmt {
            Statement::Empty => {}
            Statement::UnsafeAsm(lines) => {
                for line in lines {
                    emit_asm_line(line, &mut main_code)?;
                }
            }
        }
    }

    if main_code.last() != Some(&(OPCODE_RET as u8)) {
        main_code.push(OPCODE_RET as u8);
    }

    let mut stub: Vec<u8> = Vec::new();
    stub.push(OPCODE_CALL as u8);

    stub.extend(&[0u8, 0u8, 0u8, 0u8]);

    stub.extend(&0u32.to_le_bytes());

    stub.push(OPCODE_HALT as u8);

    let data_table_size: u32 = 0;

    let main_addr = (HEADER_FIXED_SIZE + data_table_size) + (stub.len() as u32);
    let addr_bytes = main_addr.to_le_bytes();

    stub[1] = addr_bytes[0];
    stub[2] = addr_bytes[1];
    stub[3] = addr_bytes[2];
    stub[4] = addr_bytes[3];

    code.extend(stub);
    code.extend(main_code);

    let mut out: Vec<u8> = Vec::new();
    out.push(((MAGIC >> 16) & 0xFF) as u8);
    out.push(((MAGIC >> 8) & 0xFF) as u8);
    out.push(((MAGIC >> 0) & 0xFF) as u8);
    out.push(0u8);
    out.extend(&data_table_size.to_le_bytes());
    out.extend(code);
    Ok(out)
}
