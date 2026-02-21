use crate::ast::{Instruction, Operand, Program, Statement};

include!(concat!(env!("OUT_DIR"), "/opcodes.rs"));

pub fn emit_program(prog: &Program) -> Result<Vec<u8>, String> {
    let mut code: Vec<u8> = Vec::new();

    let main_fn = prog
        .functions
        .iter()
        .find(|f| f.name == "main")
        .ok_or("missing main")?;

    for stmt in &main_fn.body {
        match stmt {
            Statement::Empty => {}
            Statement::Instr(instr) => {
                emit_instr(instr, &mut code)?;
            }
            Statement::UnsafeBlock(inner) => {
                for s in inner {
                    if let Statement::Instr(i) = s {
                        emit_instr(i, &mut code)?;
                    }
                }
            }
        }
    }

    let mut out: Vec<u8> = Vec::new();
    out.push(((MAGIC >> 16) & 0xFF) as u8);
    out.push(((MAGIC >> 8) & 0xFF) as u8);
    out.push(((MAGIC >> 0) & 0xFF) as u8);
    out.push(0u8);
    let data_table_size: u32 = 0;
    out.extend(&data_table_size.to_le_bytes());
    out.extend(code);
    Ok(out)
}

fn emit_instr(instr: &Instruction, code: &mut Vec<u8>) -> Result<(), String> {
    let name = instr.name.to_uppercase();
    match name.as_str() {
        "WRITE" => {
            if instr.args.len() != 2 {
                return Err("WRITE requires 2 args".into());
            }
            let fd = match &instr.args[0] {
                Operand::Imm(n) => *n as u8,
                Operand::Reg(r) => *r,
                _ => return Err("Invalid fd".into()),
            };
            let s = match &instr.args[1] {
                Operand::Str(s) => s.clone(),
                _ => return Err("WRITE requires string".into()),
            };
            code.push(OPCODE_WRITE as u8);
            code.push(fd);
            if s.len() > 255 {
                return Err("string too long".into());
            }
            code.push(s.len() as u8);
            code.extend(s.as_bytes());
            Ok(())
        }
        "PRINTLN" => {
            if instr.args.len() == 0 {
                code.push(OPCODE_NEWLINE as u8);
                return Ok(());
            }
            if instr.args.len() == 1 {
                match &instr.args[0] {
                    Operand::Str(s) => {
                        code.push(OPCODE_WRITE as u8);
                        code.push(1u8);
                        if s.len() > 255 {
                            return Err("string too long".into());
                        }
                        code.push(s.len() as u8);
                        code.extend(s.as_bytes());
                        code.push(OPCODE_NEWLINE as u8);
                        return Ok(());
                    }
                    Operand::Char(c) => {
                        code.push(OPCODE_PRINT as u8);
                        code.push(*c);
                        code.push(OPCODE_NEWLINE as u8);
                        return Ok(());
                    }
                    _ => return Err("PRINTLN expects a string or char".into()),
                }
            }
            Err("PRINTLN accepts 0 or 1 argument".into())
        }
        "NEWLINE" => {
            code.push(OPCODE_NEWLINE as u8);
            Ok(())
        }
        "HALT" => {
            code.push(OPCODE_HALT as u8);
            Ok(())
        }
        "PRINT" => {
            if instr.args.len() != 1 {
                return Err("PRINT requires 1 arg".into());
            }
            let c = match &instr.args[0] {
                Operand::Char(c) => *c,
                Operand::Imm(n) => *n as u8,
                _ => return Err("PRINT expects char or immediate".into()),
            };
            code.push(OPCODE_PRINT as u8);
            code.push(c);
            Ok(())
        }
        "MOV" => {
            if instr.args.len() != 2 {
                return Err("MOV requires 2 args".into());
            }
            let dst = match &instr.args[0] {
                Operand::Reg(r) => *r,
                _ => return Err("MOV dst must be reg".into()),
            };
            match &instr.args[1] {
                Operand::Imm(n) => {
                    code.push(OPCODE_MOV_IMM as u8);
                    code.push(dst);
                    code.extend((*n as i32).to_le_bytes().iter());
                    Ok(())
                }
                Operand::Reg(r) => {
                    code.push(OPCODE_MOV_REG as u8);
                    code.push(dst);
                    code.push(*r);
                    Ok(())
                }
                _ => Err("MOV src must be reg or imm".into()),
            }
        }
        "ADD" => {
            if instr.args.len() != 2 {
                return Err("ADD requires 2 args".into());
            }
            let dst = match &instr.args[0] {
                Operand::Reg(r) => *r,
                _ => return Err("ADD dst must be reg".into()),
            };
            let src = match &instr.args[1] {
                Operand::Reg(r) => *r,
                _ => return Err("ADD src must be reg".into()),
            };
            code.push(OPCODE_ADD as u8);
            code.push(dst);
            code.push(src);
            Ok(())
        }
        "PUSH" => {
            if instr.args.len() != 1 {
                return Err("PUSH requires 1 arg".into());
            }
            match &instr.args[0] {
                Operand::Imm(n) => {
                    code.push(OPCODE_PUSH_IMM as u8);
                    code.extend((*n as i32).to_le_bytes().iter());
                    Ok(())
                }
                Operand::Reg(r) => {
                    code.push(OPCODE_PUSH_REG as u8);
                    code.push(*r);
                    Ok(())
                }
                _ => Err("PUSH expects imm or reg".into()),
            }
        }
        "POP" => {
            if instr.args.len() != 1 {
                return Err("POP requires 1 arg".into());
            }
            let r = match &instr.args[0] {
                Operand::Reg(r) => *r,
                _ => return Err("POP expects reg".into()),
            };
            code.push(OPCODE_POP as u8);
            code.push(r);
            Ok(())
        }
        "PRINTREG" => {
            if instr.args.len() != 1 {
                return Err("PRINTREG requires 1 arg".into());
            }
            let r = match &instr.args[0] {
                Operand::Reg(r) => *r,
                _ => return Err("PRINTREG expects reg".into()),
            };
            code.push(OPCODE_PRINTREG as u8);
            code.push(r);
            Ok(())
        }
        other => Err(format!("Unsupported instruction: {}", other)),
    }
}
