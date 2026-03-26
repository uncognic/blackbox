include!(concat!(env!("OUT_DIR"), "/opcodes.rs"));

#[derive(Debug, PartialEq)]
enum Operand {
    Imm(i64),
    Reg(u8),
    Str(String),
    Char(u8),
}

#[derive(Debug, PartialEq)]
struct Instruction {
    name: String,
    args: Vec<Operand>,
}

#[derive(Clone, Copy)]
enum OperandKind {
    Reg,
    Imm,
    Str,
    Char,
    FdLiteral,
    FileFd,
}

struct InstrDef {
    name: &'static str,
    operands: &'static [OperandKind],
    emit: fn(&[Operand], &mut Vec<u8>) -> Result<(), String>,
}

const OP0: &[OperandKind] = &[];
const OP_REG: &[OperandKind] = &[OperandKind::Reg];
const OP_IMM: &[OperandKind] = &[OperandKind::Imm];
const OP_REG_IMM: &[OperandKind] = &[OperandKind::Reg, OperandKind::Imm];
const OP_REG_IMM_IMM: &[OperandKind] = &[OperandKind::Reg, OperandKind::Imm, OperandKind::Imm];
const OP_REG_REG: &[OperandKind] = &[OperandKind::Reg, OperandKind::Reg];

pub fn emit_asm_line(line: &str, code: &mut Vec<u8>) -> Result<(), String> {
    if let Some(instr) = parse_asm_line(line)? {
        emit_instr(&instr, code)?;
    }
    Ok(())
}

fn parse_asm_line(line: &str) -> Result<Option<Instruction>, String> {
    let no_comment = line.split(';').next().unwrap_or("");
    let s = no_comment.trim();
    if s.is_empty() {
        return Ok(None);
    }

    let mut parts = s.splitn(2, char::is_whitespace);
    let op = parts.next().unwrap_or_default();
    let rest = parts.next().unwrap_or("").trim();
    let upper = op.to_uppercase();

    if upper == "HALT" && !rest.is_empty() {
        let code = parse_halt_code(rest)?;
        return Ok(Some(Instruction {
            name: upper,
            args: vec![Operand::Imm(code)],
        }));
    }

    let raw_args = split_csv(rest)?;
    let candidates = INSTRUCTIONS
        .iter()
        .enumerate()
        .filter(|(_, d)| d.name.eq_ignore_ascii_case(&upper) && d.operands.len() == raw_args.len())
        .map(|(i, _)| i)
        .collect::<Vec<_>>();

    if candidates.is_empty() {
        return match instr_name_exists(&upper) {
            true => Err(format!(
                "{} does not support {} operand(s)",
                upper,
                raw_args.len()
            )),
            false => Err(format!("Unsupported raw asm instruction: {}", op)),
        };
    }

    let mut last_parse_err: Option<String> = None;
    for def_index in candidates {
        let def = &INSTRUCTIONS[def_index];
        match parse_operands_by_kind(&raw_args, def.operands) {
            Ok(args) => {
                return Ok(Some(Instruction { name: upper, args }));
            }
            Err(err) => last_parse_err = Some(err),
        }
    }

    match last_parse_err {
        Some(err) => Err(err),
        None if instr_name_exists(&upper) => Err(format!(
            "{} does not support {} operand(s)",
            upper,
            raw_args.len()
        )),
        None => Err(format!("Unsupported raw asm instruction: {}", op)),
    }
}

fn parse_operands_by_kind(
    raw_args: &[&str],
    kinds: &[OperandKind],
) -> Result<Vec<Operand>, String> {
    let mut args = Vec::with_capacity(raw_args.len());
    for (tok, kind) in raw_args.iter().zip(kinds.iter()) {
        args.push(parse_operand_as(tok, *kind)?);
    }
    Ok(args)
}

fn parse_halt_code(tok: &str) -> Result<i64, String> {
    let t = tok.trim();
    if t.eq_ignore_ascii_case("ok") {
        return Ok(0);
    }
    if t.eq_ignore_ascii_case("bad") {
        return Ok(1);
    }

    if t.starts_with("0x") || t.starts_with("0X") {
        return i64::from_str_radix(&t[2..], 16)
            .map_err(|_| format!("invalid HALT operand: {}", tok));
    }
    t.parse::<i64>()
        .map_err(|_| format!("invalid HALT operand: {}", tok))
}

fn split_csv(s: &str) -> Result<Vec<&str>, String> {
    if s.trim().is_empty() {
        return Ok(Vec::new());
    }

    let mut out = Vec::new();
    let mut start = 0usize;
    let mut in_quote = false;
    let mut in_char = false;
    let bytes = s.as_bytes();
    let mut i = 0usize;

    while i < bytes.len() {
        let b = bytes[i];
        if b == b'"' && !in_char {
            in_quote = !in_quote;
        } else if b == b'\'' && !in_quote {
            in_char = !in_char;
        } else if b == b',' && !in_quote && !in_char {
            let part = s[start..i].trim();
            if part.is_empty() {
                return Err("empty operand".into());
            }
            out.push(part);
            start = i + 1;
        }
        i += 1;
    }

    if in_quote || in_char {
        return Err("unterminated quoted operand".into());
    }

    let tail = s[start..].trim();
    if tail.is_empty() {
        return Err("empty operand".into());
    }
    out.push(tail);
    Ok(out)
}

fn parse_register_operand(tok: &str) -> Result<Operand, String> {
    let t = tok.trim();
    if t.len() >= 2 && (t.starts_with('R') || t.starts_with('r')) {
        let reg = t[1..]
            .parse::<u8>()
            .map_err(|_| format!("invalid register: {}", tok))?;
        Ok(Operand::Reg(reg))
    } else {
        Err(format!("expected register, got: {}", tok))
    }
}

fn parse_immediate_operand(tok: &str) -> Result<Operand, String> {
    let t = tok.trim();
    if t.starts_with('\'') && t.ends_with('\'') && t.len() >= 3 {
        return Ok(Operand::Imm(t.as_bytes()[1] as i64));
    }

    let n = if t.starts_with("0x") || t.starts_with("0X") {
        i64::from_str_radix(&t[2..], 16).map_err(|_| format!("invalid immediate: {}", tok))?
    } else {
        t.parse::<i64>()
            .map_err(|_| format!("invalid immediate: {}", tok))?
    };
    Ok(Operand::Imm(n))
}

fn parse_string_operand(tok: &str) -> Result<Operand, String> {
    let t = tok.trim();
    if t.len() < 2 || !t.starts_with('"') || !t.ends_with('"') {
        return Err(format!("expected quoted string, got: {}", tok));
    }
    Ok(Operand::Str(t[1..t.len() - 1].to_string()))
}

fn parse_char_operand(tok: &str) -> Result<Operand, String> {
    let t = tok.trim();
    if t.len() < 3 || !t.starts_with('\'') || !t.ends_with('\'') {
        return Err(format!("expected char literal, got: {}", tok));
    }
    Ok(Operand::Char(t.as_bytes()[1]))
}

fn parse_fd_literal_operand(tok: &str) -> Result<Operand, String> {
    let t = tok.trim();
    if t.eq_ignore_ascii_case("stdout") {
        return Ok(Operand::Imm(1));
    }
    if t.eq_ignore_ascii_case("stderr") {
        return Ok(Operand::Imm(2));
    }
    parse_immediate_operand(t)
}

fn parse_file_fd_operand(tok: &str) -> Result<Operand, String> {
    let t = tok.trim();
    if t.len() >= 2 && (t.starts_with('F') || t.starts_with('f')) {
        let fd = t[1..]
            .parse::<u8>()
            .map_err(|_| format!("invalid file descriptor: {}", tok))?;
        return Ok(Operand::Imm(fd as i64));
    }
    Err(format!("expected file descriptor (F0 style), got: {}", tok))
}

fn parse_operand_as(tok: &str, kind: OperandKind) -> Result<Operand, String> {
    match kind {
        OperandKind::Reg => parse_register_operand(tok),
        OperandKind::Imm => parse_immediate_operand(tok),
        OperandKind::Str => parse_string_operand(tok),
        OperandKind::Char => parse_char_operand(tok),
        OperandKind::FdLiteral => parse_fd_literal_operand(tok),
        OperandKind::FileFd => parse_file_fd_operand(tok),
    }
}

fn instr_name_exists(name: &str) -> bool {
    INSTRUCTIONS
        .iter()
        .any(|d| d.name.eq_ignore_ascii_case(name))
}

fn as_reg(op: &Operand, ctx: &str) -> Result<u8, String> {
    match op {
        Operand::Reg(r) => Ok(*r),
        _ => Err(format!("{} expects register", ctx)),
    }
}

fn as_imm_i64(op: &Operand, ctx: &str) -> Result<i64, String> {
    match op {
        Operand::Imm(n) => Ok(*n),
        _ => Err(format!("{} expects immediate", ctx)),
    }
}

fn emit_zero_impl(
    ops: &[Operand],
    code: &mut Vec<u8>,
    opcode: u8,
    ctx: &str,
) -> Result<(), String> {
    if !ops.is_empty() {
        return Err(format!("{} requires 0 operands", ctx));
    }
    code.push(opcode);
    Ok(())
}

fn emit_one_reg_impl(
    ops: &[Operand],
    code: &mut Vec<u8>,
    opcode: u8,
    ctx: &str,
) -> Result<(), String> {
    if ops.len() != 1 {
        return Err(format!("{} requires 1 operand", ctx));
    }
    code.push(opcode);
    code.push(as_reg(&ops[0], ctx)?);
    Ok(())
}

fn emit_two_reg_impl(
    ops: &[Operand],
    code: &mut Vec<u8>,
    opcode: u8,
    ctx: &str,
) -> Result<(), String> {
    if ops.len() != 2 {
        return Err(format!("{} requires 2 operands", ctx));
    }
    code.push(opcode);
    code.push(as_reg(&ops[0], ctx)?);
    code.push(as_reg(&ops[1], ctx)?);
    Ok(())
}

fn emit_one_imm_impl(
    ops: &[Operand],
    code: &mut Vec<u8>,
    opcode: u8,
    ctx: &str,
) -> Result<(), String> {
    if ops.len() != 1 {
        return Err(format!("{} requires 1 operand", ctx));
    }
    code.push(opcode);
    let imm = as_imm_i64(&ops[0], ctx)? as i32;
    code.extend(imm.to_le_bytes());
    Ok(())
}

fn emit_reg_imm_impl(
    ops: &[Operand],
    code: &mut Vec<u8>,
    opcode: u8,
    ctx: &str,
) -> Result<(), String> {
    if ops.len() != 2 {
        return Err(format!("{} requires 2 operands", ctx));
    }
    let dst = as_reg(&ops[0], ctx)?;
    let imm = as_imm_i64(&ops[1], ctx)? as i32;
    code.push(opcode);
    code.push(dst);
    code.extend(imm.to_le_bytes());
    Ok(())
}

fn emit_filefd_reg_impl(
    ops: &[Operand],
    code: &mut Vec<u8>,
    opcode: u8,
    ctx: &str,
) -> Result<(), String> {
    if ops.len() != 2 {
        return Err(format!("{} requires 2 operands", ctx));
    }
    let fd = as_imm_i64(&ops[0], ctx)? as u8;
    let src = as_reg(&ops[1], ctx)?;
    code.push(opcode);
    code.push(fd);
    code.push(src);
    Ok(())
}

fn emit_filefd_imm_impl(
    ops: &[Operand],
    code: &mut Vec<u8>,
    opcode: u8,
    ctx: &str,
) -> Result<(), String> {
    if ops.len() != 2 {
        return Err(format!("{} requires 2 operands", ctx));
    }
    let fd = as_imm_i64(&ops[0], ctx)? as u8;
    let imm = as_imm_i64(&ops[1], ctx)? as i32;
    code.push(opcode);
    code.push(fd);
    code.extend(imm.to_le_bytes());
    Ok(())
}

macro_rules! define_emit_zero {
    ($fn_name:ident, $opcode:expr, $ctx:expr) => {
        fn $fn_name(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
            emit_zero_impl(ops, code, $opcode as u8, $ctx)
        }
    };
}

macro_rules! define_emit_one_reg {
    ($fn_name:ident, $opcode:expr, $ctx:expr) => {
        fn $fn_name(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
            emit_one_reg_impl(ops, code, $opcode as u8, $ctx)
        }
    };
}

macro_rules! define_emit_two_reg {
    ($fn_name:ident, $opcode:expr, $ctx:expr) => {
        fn $fn_name(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
            emit_two_reg_impl(ops, code, $opcode as u8, $ctx)
        }
    };
}

macro_rules! define_emit_one_imm {
    ($fn_name:ident, $opcode:expr, $ctx:expr) => {
        fn $fn_name(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
            emit_one_imm_impl(ops, code, $opcode as u8, $ctx)
        }
    };
}

macro_rules! define_emit_reg_imm {
    ($fn_name:ident, $opcode:expr, $ctx:expr) => {
        fn $fn_name(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
            emit_reg_imm_impl(ops, code, $opcode as u8, $ctx)
        }
    };
}

macro_rules! define_emit_filefd_reg {
    ($fn_name:ident, $opcode:expr, $ctx:expr) => {
        fn $fn_name(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
            emit_filefd_reg_impl(ops, code, $opcode as u8, $ctx)
        }
    };
}

macro_rules! define_emit_filefd_imm {
    ($fn_name:ident, $opcode:expr, $ctx:expr) => {
        fn $fn_name(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
            emit_filefd_imm_impl(ops, code, $opcode as u8, $ctx)
        }
    };
}

fn emit_print(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 1 {
        return Err("PRINT requires 1 operand".into());
    }
    let c = match &ops[0] {
        Operand::Char(c) => *c,
        Operand::Imm(n) => *n as u8,
        _ => return Err("PRINT expects char or immediate".into()),
    };
    code.push(OPCODE_PRINT as u8);
    code.push(c);
    Ok(())
}

fn emit_write(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 2 {
        return Err("WRITE requires 2 operands".into());
    }
    let fd = as_imm_i64(&ops[0], "WRITE")? as u8;
    let s = match &ops[1] {
        Operand::Str(s) => s,
        _ => return Err("WRITE expects quoted string for second operand".into()),
    };
    if s.len() > 255 {
        return Err("string too long".into());
    }
    code.push(OPCODE_WRITE as u8);
    code.push(fd);
    code.push(s.len() as u8);
    code.extend(s.as_bytes());
    Ok(())
}

fn emit_fopen(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 3 {
        return Err("FOPEN requires 3 operands".into());
    }
    let mode = match ops[0] {
        Operand::Char(c) => c as char,
        _ => return Err("FOPEN mode must be char literal ('r', 'w', or 'a')".into()),
    };
    let mode_flag = match mode {
        'r' | 'R' => 0u8,
        'w' | 'W' => 1u8,
        'a' | 'A' => 2u8,
        _ => return Err("FOPEN mode must be 'r', 'w', or 'a'".into()),
    };
    let fd = as_imm_i64(&ops[1], "FOPEN")? as u8;
    let filename = match &ops[2] {
        Operand::Str(s) => s,
        _ => return Err("FOPEN filename must be quoted string".into()),
    };
    if filename.len() > 255 {
        return Err("filename too long".into());
    }
    code.push(OPCODE_FOPEN as u8);
    code.push(mode_flag);
    code.push(fd);
    code.push(filename.len() as u8);
    code.extend(filename.as_bytes());
    Ok(())
}

fn emit_fclose(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 1 {
        return Err("FCLOSE requires 1 operand".into());
    }
    let fd = as_imm_i64(&ops[0], "FCLOSE")? as u8;
    code.push(OPCODE_FCLOSE as u8);
    code.push(fd);
    Ok(())
}

fn emit_fread(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 2 {
        return Err("FREAD requires 2 operands".into());
    }
    let fd = as_imm_i64(&ops[0], "FREAD")? as u8;
    let reg = as_reg(&ops[1], "FREAD")?;
    code.push(OPCODE_FREAD as u8);
    code.push(fd);
    code.push(reg);
    Ok(())
}

fn emit_call(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 1 {
        return Err("CALL requires 1 operand".into());
    }
    let addr = as_imm_i64(&ops[0], "CALL")? as i32;
    code.push(OPCODE_CALL as u8);
    code.extend(addr.to_le_bytes());
    code.extend(0u32.to_le_bytes());
    Ok(())
}

fn emit_halt_with_code(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 1 {
        return Err("HALT requires 1 operand".into());
    }
    let v = as_imm_i64(&ops[0], "HALT")? as u8;
    code.push(OPCODE_HALT as u8);
    code.push(v);
    Ok(())
}

fn emit_rand_unbounded(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 1 {
        return Err("RAND requires 1 operand in source compiler".into());
    }
    let reg = as_reg(&ops[0], "RAND")?;
    code.push(OPCODE_RAND as u8);
    code.push(reg);
    code.extend(i64::MIN.to_le_bytes());
    code.extend(i64::MAX.to_le_bytes());
    Ok(())
}

fn emit_rand_max(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 2 {
        return Err("RAND requires 2 operands".into());
    }
    let reg = as_reg(&ops[0], "RAND")?;
    let max = as_imm_i64(&ops[1], "RAND")?;
    code.push(OPCODE_RAND as u8);
    code.push(reg);
    code.extend(0i64.to_le_bytes());
    code.extend(max.to_le_bytes());
    Ok(())
}

fn emit_rand_min_max(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 3 {
        return Err("RAND requires 3 operands".into());
    }
    let reg = as_reg(&ops[0], "RAND")?;
    let min = as_imm_i64(&ops[1], "RAND")?;
    let max = as_imm_i64(&ops[2], "RAND")?;
    code.push(OPCODE_RAND as u8);
    code.push(reg);
    code.extend(min.to_le_bytes());
    code.extend(max.to_le_bytes());
    Ok(())
}

fn emit_exec(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    if ops.len() != 2 {
        return Err("EXEC requires 2 operands".into());
    }
    let cmd = match &ops[0] {
        Operand::Str(s) => s,
        _ => return Err("EXEC first operand must be quoted string".into()),
    };
    if cmd.len() > 255 {
        return Err("EXEC command too long".into());
    }
    let dest = as_reg(&ops[1], "EXEC")?;
    code.push(OPCODE_EXEC as u8);
    code.push(dest);
    code.push(cmd.len() as u8);
    code.extend(cmd.as_bytes());
    Ok(())
}

fn emit_load_data_impl(
    ops: &[Operand],
    code: &mut Vec<u8>,
    opcode: u8,
    ctx: &str,
) -> Result<(), String> {
    if ops.len() != 2 {
        return Err(format!("{} requires 2 operands", ctx));
    }
    let reg = as_reg(&ops[0], ctx)?;
    let offset = as_imm_i64(&ops[1], ctx)? as i32;
    code.push(opcode);
    code.push(reg);
    code.extend(offset.to_le_bytes());
    Ok(())
}

fn emit_loadstr(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    emit_load_data_impl(ops, code, OPCODE_LOADSTR as u8, "LOADSTR")
}

fn emit_loadbyte(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    emit_load_data_impl(ops, code, OPCODE_LOADBYTE as u8, "LOADBYTE")
}

fn emit_loadword(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    emit_load_data_impl(ops, code, OPCODE_LOADWORD as u8, "LOADWORD")
}

fn emit_loaddword(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    emit_load_data_impl(ops, code, OPCODE_LOADDWORD as u8, "LOADDWORD")
}

fn emit_loadqword(ops: &[Operand], code: &mut Vec<u8>) -> Result<(), String> {
    emit_load_data_impl(ops, code, OPCODE_LOADQWORD as u8, "LOADQWORD")
}

define_emit_zero!(emit_newline, OPCODE_NEWLINE, "NEWLINE");
define_emit_zero!(emit_halt, OPCODE_HALT, "HALT");
define_emit_zero!(emit_ret, OPCODE_RET, "RET");
define_emit_zero!(emit_clrscr, OPCODE_CLRSCR, "CLRSCR");
define_emit_zero!(emit_continue, OPCODE_CONTINUE, "CONTINUE");
define_emit_zero!(emit_break, OPCODE_BREAK, "BREAK");
define_emit_zero!(
    emit_print_stacksize,
    OPCODE_PRINT_STACKSIZE,
    "PRINT_STACKSIZE"
);

define_emit_one_reg!(emit_printreg, OPCODE_PRINTREG, "PRINTREG");
define_emit_one_reg!(emit_pop, OPCODE_POP, "POP");
define_emit_one_reg!(emit_inc, OPCODE_INC, "INC");
define_emit_one_reg!(emit_dec, OPCODE_DEC, "DEC");
define_emit_one_reg!(emit_not, OPCODE_NOT, "NOT");
define_emit_one_reg!(emit_readchar, OPCODE_READCHAR, "READCHAR");
define_emit_one_reg!(emit_readstr, OPCODE_READSTR, "READSTR");
define_emit_one_reg!(emit_read, OPCODE_READ, "READ");
define_emit_one_reg!(emit_getkey, OPCODE_GETKEY, "GETKEY");
define_emit_one_reg!(emit_printstr, OPCODE_PRINTSTR, "PRINTSTR");

define_emit_two_reg!(emit_add, OPCODE_ADD, "ADD");
define_emit_two_reg!(emit_sub, OPCODE_SUB, "SUB");
define_emit_two_reg!(emit_mul, OPCODE_MUL, "MUL");
define_emit_two_reg!(emit_div, OPCODE_DIV, "DIV");
define_emit_two_reg!(emit_mod, OPCODE_MOD, "MOD");
define_emit_two_reg!(emit_and, OPCODE_AND, "AND");
define_emit_two_reg!(emit_or, OPCODE_OR, "OR");
define_emit_two_reg!(emit_xor, OPCODE_XOR, "XOR");
define_emit_two_reg!(emit_cmp, OPCODE_CMP, "CMP");
define_emit_two_reg!(emit_mov, OPCODE_MOV_REG, "MOV");
define_emit_reg_imm!(emit_movi, OPCODE_MOVI, "MOVI");
define_emit_one_reg!(emit_push, OPCODE_PUSH_REG, "PUSH");
define_emit_one_imm!(emit_pushi, OPCODE_PUSHI, "PUSHI");
define_emit_two_reg!(emit_load_reg, OPCODE_LOAD_REG, "LOAD");
define_emit_reg_imm!(emit_load_imm, OPCODE_LOAD, "LOAD");
define_emit_two_reg!(emit_store_reg, OPCODE_STORE_REG, "STORE");
define_emit_reg_imm!(emit_store_imm, OPCODE_STORE, "STORE");
define_emit_two_reg!(emit_loadvar_reg, OPCODE_LOADVAR_REG, "LOADVAR");
define_emit_reg_imm!(emit_loadvar_imm, OPCODE_LOADVAR, "LOADVAR");
define_emit_two_reg!(emit_storevar_reg, OPCODE_STOREVAR_REG, "STOREVAR");
define_emit_reg_imm!(emit_storevar_imm, OPCODE_STOREVAR, "STOREVAR");
define_emit_filefd_reg!(emit_fseek_reg, OPCODE_FSEEK_REG, "FSEEK");
define_emit_filefd_imm!(emit_fseek_imm, OPCODE_FSEEK_IMM, "FSEEK");
define_emit_filefd_reg!(emit_fwrite_reg, OPCODE_FWRITE_REG, "FWRITE");
define_emit_filefd_imm!(emit_fwrite_imm, OPCODE_FWRITE_IMM, "FWRITE");

define_emit_one_imm!(emit_jmp, OPCODE_JMP, "JMP");
define_emit_one_imm!(emit_je, OPCODE_JE, "JE");
define_emit_one_imm!(emit_jne, OPCODE_JNE, "JNE");
define_emit_one_imm!(emit_jl, OPCODE_JL, "JL");
define_emit_one_imm!(emit_jge, OPCODE_JGE, "JGE");
define_emit_one_imm!(emit_jb, OPCODE_JB, "JB");
define_emit_one_imm!(emit_jae, OPCODE_JAE, "JAE");
define_emit_one_imm!(emit_jmpi, OPCODE_JMPI, "JMPI");
define_emit_one_imm!(emit_alloc, OPCODE_ALLOC, "ALLOC");
define_emit_one_imm!(emit_grow, OPCODE_GROW, "GROW");
define_emit_one_imm!(emit_resize, OPCODE_RESIZE, "RESIZE");
define_emit_one_imm!(emit_free, OPCODE_FREE, "FREE");
define_emit_one_imm!(emit_sleep, OPCODE_SLEEP, "SLEEP");

static INSTRUCTIONS: &[InstrDef] = &[
    InstrDef {
        name: "NEWLINE",
        operands: OP0,
        emit: emit_newline,
    },
    InstrDef {
        name: "HALT",
        operands: OP0,
        emit: emit_halt,
    },
    InstrDef {
        name: "HALT",
        operands: OP_IMM,
        emit: emit_halt_with_code,
    },
    InstrDef {
        name: "RET",
        operands: OP0,
        emit: emit_ret,
    },
    InstrDef {
        name: "CLRSCR",
        operands: OP0,
        emit: emit_clrscr,
    },
    InstrDef {
        name: "CONTINUE",
        operands: OP0,
        emit: emit_continue,
    },
    InstrDef {
        name: "BREAK",
        operands: OP0,
        emit: emit_break,
    },
    InstrDef {
        name: "PRINT_STACKSIZE",
        operands: OP0,
        emit: emit_print_stacksize,
    },
    InstrDef {
        name: "PRINTREG",
        operands: OP_REG,
        emit: emit_printreg,
    },
    InstrDef {
        name: "POP",
        operands: OP_REG,
        emit: emit_pop,
    },
    InstrDef {
        name: "INC",
        operands: OP_REG,
        emit: emit_inc,
    },
    InstrDef {
        name: "DEC",
        operands: OP_REG,
        emit: emit_dec,
    },
    InstrDef {
        name: "NOT",
        operands: OP_REG,
        emit: emit_not,
    },
    InstrDef {
        name: "READCHAR",
        operands: OP_REG,
        emit: emit_readchar,
    },
    InstrDef {
        name: "READSTR",
        operands: OP_REG,
        emit: emit_readstr,
    },
    InstrDef {
        name: "READ",
        operands: OP_REG,
        emit: emit_read,
    },
    InstrDef {
        name: "GETKEY",
        operands: OP_REG,
        emit: emit_getkey,
    },
    InstrDef {
        name: "PRINTSTR",
        operands: OP_REG,
        emit: emit_printstr,
    },
    InstrDef {
        name: "ADD",
        operands: OP_REG_REG,
        emit: emit_add,
    },
    InstrDef {
        name: "SUB",
        operands: OP_REG_REG,
        emit: emit_sub,
    },
    InstrDef {
        name: "MUL",
        operands: OP_REG_REG,
        emit: emit_mul,
    },
    InstrDef {
        name: "DIV",
        operands: OP_REG_REG,
        emit: emit_div,
    },
    InstrDef {
        name: "MOD",
        operands: OP_REG_REG,
        emit: emit_mod,
    },
    InstrDef {
        name: "AND",
        operands: OP_REG_REG,
        emit: emit_and,
    },
    InstrDef {
        name: "OR",
        operands: OP_REG_REG,
        emit: emit_or,
    },
    InstrDef {
        name: "XOR",
        operands: OP_REG_REG,
        emit: emit_xor,
    },
    InstrDef {
        name: "CMP",
        operands: OP_REG_REG,
        emit: emit_cmp,
    },
    InstrDef {
        name: "MOV",
        operands: OP_REG_REG,
        emit: emit_mov,
    },
    InstrDef {
        name: "MOVI",
        operands: OP_REG_IMM,
        emit: emit_movi,
    },
    InstrDef {
        name: "PUSH",
        operands: OP_REG,
        emit: emit_push,
    },
    InstrDef {
        name: "PUSHI",
        operands: OP_IMM,
        emit: emit_pushi,
    },
    InstrDef {
        name: "LOAD",
        operands: OP_REG_REG,
        emit: emit_load_reg,
    },
    InstrDef {
        name: "LOAD",
        operands: OP_REG_IMM,
        emit: emit_load_imm,
    },
    InstrDef {
        name: "STORE",
        operands: OP_REG_REG,
        emit: emit_store_reg,
    },
    InstrDef {
        name: "STORE",
        operands: OP_REG_IMM,
        emit: emit_store_imm,
    },
    InstrDef {
        name: "LOADVAR",
        operands: OP_REG_REG,
        emit: emit_loadvar_reg,
    },
    InstrDef {
        name: "LOADVAR",
        operands: OP_REG_IMM,
        emit: emit_loadvar_imm,
    },
    InstrDef {
        name: "STOREVAR",
        operands: OP_REG_REG,
        emit: emit_storevar_reg,
    },
    InstrDef {
        name: "STOREVAR",
        operands: OP_REG_IMM,
        emit: emit_storevar_imm,
    },
    InstrDef {
        name: "FSEEK",
        operands: &[OperandKind::FileFd, OperandKind::Reg],
        emit: emit_fseek_reg,
    },
    InstrDef {
        name: "FSEEK",
        operands: &[OperandKind::FileFd, OperandKind::Imm],
        emit: emit_fseek_imm,
    },
    InstrDef {
        name: "FWRITE",
        operands: &[OperandKind::FileFd, OperandKind::Reg],
        emit: emit_fwrite_reg,
    },
    InstrDef {
        name: "FWRITE",
        operands: &[OperandKind::FileFd, OperandKind::Imm],
        emit: emit_fwrite_imm,
    },
    InstrDef {
        name: "JMP",
        operands: OP_IMM,
        emit: emit_jmp,
    },
    InstrDef {
        name: "JE",
        operands: OP_IMM,
        emit: emit_je,
    },
    InstrDef {
        name: "JNE",
        operands: OP_IMM,
        emit: emit_jne,
    },
    InstrDef {
        name: "JL",
        operands: OP_IMM,
        emit: emit_jl,
    },
    InstrDef {
        name: "JGE",
        operands: OP_IMM,
        emit: emit_jge,
    },
    InstrDef {
        name: "JB",
        operands: OP_IMM,
        emit: emit_jb,
    },
    InstrDef {
        name: "JAE",
        operands: OP_IMM,
        emit: emit_jae,
    },
    InstrDef {
        name: "JMPI",
        operands: OP_IMM,
        emit: emit_jmpi,
    },
    InstrDef {
        name: "ALLOC",
        operands: OP_IMM,
        emit: emit_alloc,
    },
    InstrDef {
        name: "GROW",
        operands: OP_IMM,
        emit: emit_grow,
    },
    InstrDef {
        name: "RESIZE",
        operands: OP_IMM,
        emit: emit_resize,
    },
    InstrDef {
        name: "FREE",
        operands: OP_IMM,
        emit: emit_free,
    },
    InstrDef {
        name: "SLEEP",
        operands: OP_IMM,
        emit: emit_sleep,
    },
    InstrDef {
        name: "WRITE",
        operands: &[OperandKind::FdLiteral, OperandKind::Str],
        emit: emit_write,
    },
    InstrDef {
        name: "FOPEN",
        operands: &[OperandKind::Char, OperandKind::FileFd, OperandKind::Str],
        emit: emit_fopen,
    },
    InstrDef {
        name: "FCLOSE",
        operands: &[OperandKind::FileFd],
        emit: emit_fclose,
    },
    InstrDef {
        name: "FREAD",
        operands: &[OperandKind::FileFd, OperandKind::Reg],
        emit: emit_fread,
    },
    InstrDef {
        name: "CALL",
        operands: OP_IMM,
        emit: emit_call,
    },
    InstrDef {
        name: "RAND",
        operands: OP_REG,
        emit: emit_rand_unbounded,
    },
    InstrDef {
        name: "RAND",
        operands: OP_REG_IMM,
        emit: emit_rand_max,
    },
    InstrDef {
        name: "RAND",
        operands: OP_REG_IMM_IMM,
        emit: emit_rand_min_max,
    },
    InstrDef {
        name: "EXEC",
        operands: &[OperandKind::Str, OperandKind::Reg],
        emit: emit_exec,
    },
    InstrDef {
        name: "PRINT",
        operands: OP_REG,
        emit: emit_print,
    },
    InstrDef {
        name: "PRINT",
        operands: OP_IMM,
        emit: emit_print,
    },
    InstrDef {
        name: "LOADSTR",
        operands: &[OperandKind::Reg, OperandKind::Imm],
        emit: emit_loadstr,
    },
    InstrDef {
        name: "LOADBYTE",
        operands: &[OperandKind::Reg, OperandKind::Imm],
        emit: emit_loadbyte,
    },
    InstrDef {
        name: "LOADWORD",
        operands: &[OperandKind::Reg, OperandKind::Imm],
        emit: emit_loadword,
    },
    InstrDef {
        name: "LOADDWORD",
        operands: &[OperandKind::Reg, OperandKind::Imm],
        emit: emit_loaddword,
    },
    InstrDef {
        name: "LOADQWORD",
        operands: &[OperandKind::Reg, OperandKind::Imm],
        emit: emit_loadqword,
    },
];

fn emit_instr(instr: &Instruction, code: &mut Vec<u8>) -> Result<(), String> {
    let def = INSTRUCTIONS
        .iter()
        .find(|d| d.name == instr.name && d.operands.len() == instr.args.len())
        .ok_or_else(|| format!("Unsupported instruction: {}", instr.name))?;
    (def.emit)(&instr.args, code)
}
