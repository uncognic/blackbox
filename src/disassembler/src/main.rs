use std::collections::{BTreeMap, BTreeSet};
use std::env;
use std::fs::File;
use std::io::{self, BufWriter, Read, Write};

mod opcodes {
    include!(concat!(env!("OUT_DIR"), "/opcodes.rs"));
}

fn read_u32_le(bytes: &[u8], i: usize) -> Option<u32> {
    if i + 4 <= bytes.len() {
        let b = &bytes[i..i + 4];
        Some(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    } else {
        None
    }
}

fn read_i32_le(bytes: &[u8], i: usize) -> Option<i32> {
    read_u32_le(bytes, i).map(|v| v as i32)
}

fn read_u64_le(bytes: &[u8], i: usize) -> Option<u64> {
    if i + 8 <= bytes.len() {
        let mut arr = [0u8; 8];
        arr.copy_from_slice(&bytes[i..i + 8]);
        Some(u64::from_le_bytes(arr))
    } else {
        None
    }
}

fn reg(r: u8) -> String {
    format!("R{:02}", r)
}

fn label_name(addr: u32) -> String {
    format!("L_{:04X}", addr)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum DataRefKind {
    Str,
    Byte,
    Word,
    Dword,
    Qword,
}

#[derive(Debug)]
struct DataEntry {
    offset: u32,
    name: String,
    kind: DataRefKind,
}

fn scan_one(
    bytes: &[u8],
    i: usize,
    jump_targets: &mut BTreeSet<u32>,
    data_refs: &mut BTreeMap<u32, DataRefKind>,
    call_frames: &mut BTreeMap<u32, u32>,
) -> Option<usize> {
    let len = bytes.len();
    if i >= len {
        return None;
    }
    let opcode = bytes[i];
    let mut j = i + 1;

    match opcode {
        opcodes::OPCODE_WRITE => {
            if j + 2 > len {
                return None;
            }
            let slen = bytes[j + 1] as usize;
            j += 2 + slen;
        }
        opcodes::OPCODE_NEWLINE
        | opcodes::OPCODE_CLRSCR
        | opcodes::OPCODE_CONTINUE
        | opcodes::OPCODE_RET
        | opcodes::OPCODE_BREAK => {}
        opcodes::OPCODE_PRINT => {
            j += 1;
        }
        opcodes::OPCODE_PUSHI => {
            j += 4;
        }
        opcodes::OPCODE_PUSH_REG
        | opcodes::OPCODE_POP
        | opcodes::OPCODE_PRINTREG
        | opcodes::OPCODE_INC
        | opcodes::OPCODE_DEC
        | opcodes::OPCODE_PRINTSTR
        | opcodes::OPCODE_READ
        | opcodes::OPCODE_GETKEY
        | opcodes::OPCODE_READCHAR
        | opcodes::OPCODE_NOT
        | opcodes::OPCODE_READSTR
        | opcodes::OPCODE_FCLOSE => {
            j += 1;
        }
        opcodes::OPCODE_MOVI => {
            j += 1 + 4;
        }
        opcodes::OPCODE_MOV_REG
        | opcodes::OPCODE_ADD
        | opcodes::OPCODE_SUB
        | opcodes::OPCODE_MUL
        | opcodes::OPCODE_DIV
        | opcodes::OPCODE_MOD
        | opcodes::OPCODE_XOR
        | opcodes::OPCODE_AND
        | opcodes::OPCODE_OR
        | opcodes::OPCODE_CMP
        | opcodes::OPCODE_LOAD_REG
        | opcodes::OPCODE_STORE_REG
        | opcodes::OPCODE_LOADVAR_REG
        | opcodes::OPCODE_STOREVAR_REG
        | opcodes::OPCODE_FREAD
        | opcodes::OPCODE_FWRITE_REG
        | opcodes::OPCODE_FSEEK_REG => {
            j += 2;
        }
        opcodes::OPCODE_JMP | opcodes::OPCODE_JMPI => {
            if let Some(addr) = read_u32_le(bytes, j) {
                jump_targets.insert(addr);
            }
            j += 4;
        }
        opcodes::OPCODE_JE
        | opcodes::OPCODE_JNE
        | opcodes::OPCODE_JL
        | opcodes::OPCODE_JGE
        | opcodes::OPCODE_JB
        | opcodes::OPCODE_JAE => {
            if let Some(addr) = read_u32_le(bytes, j) {
                jump_targets.insert(addr);
            }
            j += 4;
        }
        opcodes::OPCODE_ALLOC
        | opcodes::OPCODE_GROW
        | opcodes::OPCODE_RESIZE
        | opcodes::OPCODE_FREE
        | opcodes::OPCODE_SLEEP => {
            j += 4;
        }
        opcodes::OPCODE_LOAD
        | opcodes::OPCODE_STORE
        | opcodes::OPCODE_LOADVAR
        | opcodes::OPCODE_STOREVAR => {
            j += 1 + 4;
        }
        opcodes::OPCODE_FOPEN => {
            if j + 3 > len {
                return None;
            }
            let fname_len = bytes[j + 2] as usize;
            j += 3 + fname_len;
        }
        opcodes::OPCODE_FWRITE_IMM | opcodes::OPCODE_FSEEK_IMM => {
            j += 1 + 4; // fd + u32
        }
        opcodes::OPCODE_LOADSTR => {
            // reg(1), offset(4)
            if j + 5 <= len {
                let off = read_u32_le(bytes, j + 1).unwrap_or(0);
                data_refs.entry(off).or_insert(DataRefKind::Str);
            }
            j += 1 + 4;
        }
        opcodes::OPCODE_LOADBYTE => {
            if j + 5 <= len {
                let off = read_u32_le(bytes, j + 1).unwrap_or(0);
                data_refs.entry(off).or_insert(DataRefKind::Byte);
            }
            j += 1 + 4;
        }
        opcodes::OPCODE_LOADWORD => {
            if j + 5 <= len {
                let off = read_u32_le(bytes, j + 1).unwrap_or(0);
                data_refs.entry(off).or_insert(DataRefKind::Word);
            }
            j += 1 + 4;
        }
        opcodes::OPCODE_LOADDWORD => {
            if j + 5 <= len {
                let off = read_u32_le(bytes, j + 1).unwrap_or(0);
                data_refs.entry(off).or_insert(DataRefKind::Dword);
            }
            j += 1 + 4;
        }
        opcodes::OPCODE_LOADQWORD => {
            if j + 5 <= len {
                let off = read_u32_le(bytes, j + 1).unwrap_or(0);
                data_refs.entry(off).or_insert(DataRefKind::Qword);
            }
            j += 1 + 4;
        }
        opcodes::OPCODE_RAND => {
            j += 1 + 8 + 8; // reg + min(u64) + max(u64)
        }
        opcodes::OPCODE_CALL => {
            if let Some(addr) = read_u32_le(bytes, j) {
                jump_targets.insert(addr);
                if let Some(frame) = read_u32_le(bytes, j + 4) {
                    if frame > 0 {
                        call_frames.insert(addr, frame);
                    }
                }
            }
            j += 4 + 4;
        }
        opcodes::OPCODE_HALT => {
            j += 1;
        }
        opcodes::OPCODE_PRINT_STACKSIZE => {}
        _ => {}
    }
    if j > len {
        return None;
    }
    Some(j)
}

fn reconstruct_data(data_table: &[u8], data_refs: &BTreeMap<u32, DataRefKind>) -> Vec<DataEntry> {
    let mut entries = Vec::new();
    let mut idx = 0usize;
    for (&offset, &kind) in data_refs {
        let name = format!("d{}", idx);
        idx += 1;
        if (offset as usize) >= data_table.len() {
            continue;
        }
        entries.push(DataEntry { offset, name, kind });
    }
    entries
}

fn data_name_for_offset(entries: &[DataEntry], offset: u32) -> String {
    for e in entries {
        if e.offset == offset {
            return format!("${}", e.name);
        }
    }
    format!("$unknown_{}", offset)
}

fn emit_data_section(w: &mut dyn Write, data_table: &[u8], entries: &[DataEntry]) {
    if entries.is_empty() {
        return;
    }
    let _ = writeln!(w, "%data");
    for e in entries {
        let off = e.offset as usize;
        match e.kind {
            DataRefKind::Str => {
                let end = data_table[off..]
                    .iter()
                    .position(|&b| b == 0)
                    .unwrap_or(data_table.len() - off);
                let s = String::from_utf8_lossy(&data_table[off..off + end]);
                let _ = writeln!(w, "    str ${}, \"{}\"", e.name, s);
            }
            DataRefKind::Byte => {
                if off < data_table.len() {
                    let _ = writeln!(w, "    byte ${}, {}", e.name, data_table[off]);
                }
            }
            DataRefKind::Word => {
                if off + 2 <= data_table.len() {
                    let v = u16::from_le_bytes([data_table[off], data_table[off + 1]]);
                    let _ = writeln!(w, "    word ${}, {}", e.name, v);
                }
            }
            DataRefKind::Dword => {
                if off + 4 <= data_table.len() {
                    let v = u32::from_le_bytes([
                        data_table[off],
                        data_table[off + 1],
                        data_table[off + 2],
                        data_table[off + 3],
                    ]);
                    let _ = writeln!(w, "    dword ${}, {}", e.name, v);
                }
            }
            DataRefKind::Qword => {
                if off + 8 <= data_table.len() {
                    let mut arr = [0u8; 8];
                    arr.copy_from_slice(&data_table[off..off + 8]);
                    let v = u64::from_le_bytes(arr);
                    let _ = writeln!(w, "    qword ${}, {}", e.name, v);
                }
            }
        }
    }
}

fn emit_instruction(
    w: &mut dyn Write,
    bytes: &[u8],
    i: usize,
    abs_offset: usize,
    jump_targets: &BTreeSet<u32>,
    data_entries: &[DataEntry],
    call_frames: &BTreeMap<u32, u32>,
) -> Option<usize> {
    let len = bytes.len();
    if i >= len {
        return None;
    }

    if jump_targets.contains(&(abs_offset as u32)) {
        let _ = writeln!(w, ".{}:", label_name(abs_offset as u32));
        if let Some(&frame) = call_frames.get(&(abs_offset as u32)) {
            if frame > 0 {
                let _ = writeln!(w, "    frame {}", frame);
            }
        }
    }

    let opcode = bytes[i];
    let mut j = i + 1;

    match opcode {
        opcodes::OPCODE_WRITE => {
            if j + 2 > len {
                return None;
            }
            let fd = bytes[j];
            let slen = bytes[j + 1] as usize;
            j += 2;
            let fd_name = match fd {
                1 => "stdout",
                2 => "stderr",
                _ => "stdout",
            };
            let s = if j + slen <= len {
                String::from_utf8_lossy(&bytes[j..j + slen]).to_string()
            } else {
                "<truncated>".to_string()
            };
            j += slen.min(len - j);
            let _ = writeln!(w, "    write {} \"{}\"", fd_name, s);
        }
        opcodes::OPCODE_NEWLINE => {
            let _ = writeln!(w, "    newline");
        }
        opcodes::OPCODE_PRINT => {
            let ch = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    print '{}'", ch as char);
        }
        opcodes::OPCODE_PUSHI => {
            if let Some(v) = read_i32_le(bytes, j) {
                let _ = writeln!(w, "    pushi {}", v);
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_PUSH_REG => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    push {}", reg(r));
        }
        opcodes::OPCODE_POP => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    pop {}", reg(r));
        }
        opcodes::OPCODE_MOVI => {
            if j + 5 > len {
                return None;
            }
            let dst = bytes[j];
            if let Some(v) = read_i32_le(bytes, j + 1) {
                let _ = writeln!(w, "    movi {}, {}", reg(dst), v);
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_MOV_REG => {
            if j + 2 > len {
                return None;
            }
            let dst = bytes[j];
            let src = bytes[j + 1];
            j += 2;
            let _ = writeln!(w, "    mov {}, {}", reg(dst), reg(src));
        }
        opcodes::OPCODE_ADD
        | opcodes::OPCODE_SUB
        | opcodes::OPCODE_MUL
        | opcodes::OPCODE_DIV
        | opcodes::OPCODE_MOD
        | opcodes::OPCODE_XOR
        | opcodes::OPCODE_AND
        | opcodes::OPCODE_OR => {
            if j + 2 > len {
                return None;
            }
            let dst = bytes[j];
            let src = bytes[j + 1];
            j += 2;
            let name = match opcode {
                x if x == opcodes::OPCODE_ADD => "add",
                x if x == opcodes::OPCODE_SUB => "sub",
                x if x == opcodes::OPCODE_MUL => "mul",
                x if x == opcodes::OPCODE_DIV => "div",
                x if x == opcodes::OPCODE_MOD => "mod",
                x if x == opcodes::OPCODE_XOR => "xor",
                x if x == opcodes::OPCODE_AND => "and",
                x if x == opcodes::OPCODE_OR => "or",
                _ => unreachable!(),
            };
            let _ = writeln!(w, "    {} {}, {}", name, reg(dst), reg(src));
        }
        opcodes::OPCODE_NOT => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    not {}", reg(r));
        }
        opcodes::OPCODE_PRINTREG => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    printreg {}", reg(r));
        }
        opcodes::OPCODE_PRINT_STACKSIZE => {
            let _ = writeln!(w, "    print_stacksize");
        }
        opcodes::OPCODE_JMP => {
            if let Some(addr) = read_u32_le(bytes, j) {
                let _ = writeln!(w, "    jmp {}", label_name(addr));
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_JMPI => {
            if let Some(addr) = read_u32_le(bytes, j) {
                let _ = writeln!(w, "    jmpi {}", addr);
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_JE | opcodes::OPCODE_JNE => {
            if let Some(addr) = read_u32_le(bytes, j) {
                let name = if opcode == opcodes::OPCODE_JE {
                    "je"
                } else {
                    "jne"
                };
                let _ = writeln!(w, "    {} {}", name, label_name(addr));
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_JL | opcodes::OPCODE_JGE | opcodes::OPCODE_JB | opcodes::OPCODE_JAE => {
            if let Some(addr) = read_u32_le(bytes, j) {
                let name = match opcode {
                    x if x == opcodes::OPCODE_JL => "jl",
                    x if x == opcodes::OPCODE_JGE => "jge",
                    x if x == opcodes::OPCODE_JB => "jb",
                    x if x == opcodes::OPCODE_JAE => "jae",
                    _ => unreachable!(),
                };
                let _ = writeln!(w, "    {} {}", name, label_name(addr));
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_INC => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    inc {}", reg(r));
        }
        opcodes::OPCODE_DEC => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    dec {}", reg(r));
        }
        opcodes::OPCODE_CMP => {
            if j + 2 > len {
                return None;
            }
            let a = bytes[j];
            let b = bytes[j + 1];
            j += 2;
            let _ = writeln!(w, "    cmp {}, {}", reg(a), reg(b));
        }
        opcodes::OPCODE_ALLOC => {
            if let Some(v) = read_u32_le(bytes, j) {
                let _ = writeln!(w, "    alloc {}", v);
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_GROW => {
            if let Some(v) = read_u32_le(bytes, j) {
                let _ = writeln!(w, "    grow {}", v);
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_RESIZE => {
            if let Some(v) = read_u32_le(bytes, j) {
                let _ = writeln!(w, "    resize {}", v);
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_FREE => {
            if let Some(v) = read_u32_le(bytes, j) {
                let _ = writeln!(w, "    free {}", v);
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_SLEEP => {
            if let Some(v) = read_u32_le(bytes, j) {
                let _ = writeln!(w, "    sleep {}", v);
                j += 4;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_LOAD => {
            if j + 5 > len {
                return None;
            }
            let r = bytes[j];
            if let Some(idx) = read_u32_le(bytes, j + 1) {
                let _ = writeln!(w, "    load {}, {}", reg(r), idx);
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_STORE => {
            if j + 5 > len {
                return None;
            }
            let r = bytes[j];
            if let Some(idx) = read_u32_le(bytes, j + 1) {
                let _ = writeln!(w, "    store {}, {}", reg(r), idx);
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_LOAD_REG => {
            if j + 2 > len {
                return None;
            }
            let a = bytes[j];
            let b = bytes[j + 1];
            j += 2;
            let _ = writeln!(w, "    load {}, {}", reg(a), reg(b));
        }
        opcodes::OPCODE_STORE_REG => {
            if j + 2 > len {
                return None;
            }
            let a = bytes[j];
            let b = bytes[j + 1];
            j += 2;
            let _ = writeln!(w, "    store {}, {}", reg(a), reg(b));
        }
        opcodes::OPCODE_LOADVAR => {
            if j + 5 > len {
                return None;
            }
            let r = bytes[j];
            if let Some(slot) = read_u32_le(bytes, j + 1) {
                let _ = writeln!(w, "    loadvar {}, {}", reg(r), slot);
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_STOREVAR => {
            if j + 5 > len {
                return None;
            }
            let r = bytes[j];
            if let Some(slot) = read_u32_le(bytes, j + 1) {
                let _ = writeln!(w, "    storevar {}, {}", reg(r), slot);
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_LOADVAR_REG => {
            if j + 2 > len {
                return None;
            }
            let a = bytes[j];
            let b = bytes[j + 1];
            j += 2;
            let _ = writeln!(w, "    loadvar {}, {}", reg(a), reg(b));
        }
        opcodes::OPCODE_STOREVAR_REG => {
            if j + 2 > len {
                return None;
            }
            let a = bytes[j];
            let b = bytes[j + 1];
            j += 2;
            let _ = writeln!(w, "    storevar {}, {}", reg(a), reg(b));
        }
        opcodes::OPCODE_FOPEN => {
            // mode(1), fd(1), fname_len(1), fname(fname_len)
            if j + 3 > len {
                return None;
            }
            let mode_flag = bytes[j];
            let fd = bytes[j + 1];
            let fname_len = bytes[j + 2] as usize;
            j += 3;
            let mode = match mode_flag {
                0 => "r",
                1 => "w",
                2 => "a",
                _ => "r",
            };
            let name = if j + fname_len <= len {
                String::from_utf8_lossy(&bytes[j..j + fname_len]).to_string()
            } else {
                "<truncated>".to_string()
            };
            j += fname_len.min(len - j);
            let _ = writeln!(w, "    fopen {}, F{}, \"{}\"", mode, fd, name);
        }
        opcodes::OPCODE_FCLOSE => {
            let fd = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    fclose F{}", fd);
        }
        opcodes::OPCODE_FREAD => {
            if j + 2 > len {
                return None;
            }
            let fd = bytes[j];
            let r = bytes[j + 1];
            j += 2;
            let _ = writeln!(w, "    fread F{}, {}", fd, reg(r));
        }
        opcodes::OPCODE_FWRITE_REG => {
            if j + 2 > len {
                return None;
            }
            let fd = bytes[j];
            let r = bytes[j + 1];
            j += 2;
            let _ = writeln!(w, "    fwrite F{}, {}", fd, reg(r));
        }
        opcodes::OPCODE_FWRITE_IMM => {
            if j + 5 > len {
                return None;
            }
            let fd = bytes[j];
            if let Some(v) = read_i32_le(bytes, j + 1) {
                let _ = writeln!(w, "    fwrite F{}, {}", fd, v);
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_FSEEK_REG => {
            if j + 2 > len {
                return None;
            }
            let fd = bytes[j];
            let r = bytes[j + 1];
            j += 2;
            let _ = writeln!(w, "    fseek F{}, {}", fd, reg(r));
        }
        opcodes::OPCODE_FSEEK_IMM => {
            if j + 5 > len {
                return None;
            }
            let fd = bytes[j];
            if let Some(v) = read_i32_le(bytes, j + 1) {
                let _ = writeln!(w, "    fseek F{}, {}", fd, v);
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_LOADSTR => {
            if j + 5 > len {
                return None;
            }
            let r = bytes[j];
            if let Some(off) = read_u32_le(bytes, j + 1) {
                let dname = data_name_for_offset(data_entries, off);
                let _ = writeln!(w, "    loadstr {}, {}", dname, reg(r));
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_LOADBYTE => {
            if j + 5 > len {
                return None;
            }
            let r = bytes[j];
            if let Some(off) = read_u32_le(bytes, j + 1) {
                let dname = data_name_for_offset(data_entries, off);
                let _ = writeln!(w, "    loadbyte {}, {}", dname, reg(r));
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_LOADWORD => {
            if j + 5 > len {
                return None;
            }
            let r = bytes[j];
            if let Some(off) = read_u32_le(bytes, j + 1) {
                let dname = data_name_for_offset(data_entries, off);
                let _ = writeln!(w, "    loadword {}, {}", dname, reg(r));
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_LOADDWORD => {
            if j + 5 > len {
                return None;
            }
            let r = bytes[j];
            if let Some(off) = read_u32_le(bytes, j + 1) {
                let dname = data_name_for_offset(data_entries, off);
                let _ = writeln!(w, "    loaddword {}, {}", dname, reg(r));
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_LOADQWORD => {
            if j + 5 > len {
                return None;
            }
            let r = bytes[j];
            if let Some(off) = read_u32_le(bytes, j + 1) {
                let dname = data_name_for_offset(data_entries, off);
                let _ = writeln!(w, "    loadqword {}, {}", dname, reg(r));
                j += 5;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_PRINTSTR => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    printstr {}", reg(r));
        }
        opcodes::OPCODE_READ => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    read {}", reg(r));
        }
        opcodes::OPCODE_READSTR => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    readstr {}", reg(r));
        }
        opcodes::OPCODE_READCHAR => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    readchar {}", reg(r));
        }
        opcodes::OPCODE_GETKEY => {
            let r = bytes.get(j).copied().unwrap_or(0);
            j += 1;
            let _ = writeln!(w, "    getkey {}", reg(r));
        }
        opcodes::OPCODE_CLRSCR => {
            let _ = writeln!(w, "    clrscr");
        }
        opcodes::OPCODE_RAND => {
            if j + 17 > len {
                return None;
            }
            let r = bytes[j];
            let min_val = read_u64_le(bytes, j + 1).unwrap_or(0);
            let max_val = read_u64_le(bytes, j + 9).unwrap_or(0);
            j += 17;
            let i64_min = i64::MIN as u64;
            let i64_max = i64::MAX as u64;
            if min_val == i64_min && max_val == i64_max {
                let _ = writeln!(w, "    rand {}", reg(r));
            } else {
                let min_i = min_val as i64;
                let max_i = max_val as i64;
                let _ = writeln!(w, "    rand {}, {}, {}", reg(r), min_i, max_i);
            }
        }
        opcodes::OPCODE_CALL => {
            if j + 8 > len {
                return None;
            }
            if let Some(addr) = read_u32_le(bytes, j) {
                let _ = writeln!(w, "    call {}", label_name(addr));
                j += 8;
            } else {
                return None;
            }
        }
        opcodes::OPCODE_RET => {
            let _ = writeln!(w, "    ret");
        }
        opcodes::OPCODE_HALT => {
            if j < len {
                let code = bytes[j];
                j += 1;
                match code {
                    0 => {
                        let _ = writeln!(w, "    halt ok");
                    }
                    1 => {
                        let _ = writeln!(w, "    halt bad");
                    }
                    _ => {
                        let _ = writeln!(w, "    halt {}", code);
                    }
                }
            } else {
                let _ = writeln!(w, "    halt");
            }
        }
        opcodes::OPCODE_BREAK => {
            let _ = writeln!(w, "    break");
        }
        opcodes::OPCODE_CONTINUE => {
            let _ = writeln!(w, "    continue");
        }
        other => {
            let _ = writeln!(w, "    ; unknown opcode 0x{:02x}", other);
        }
    }
    if j > len {
        return None;
    }
    Some(j)
}

fn decomp(data: &[u8], w: &mut dyn Write) {
    let len = data.len();

    let mut code_start = 0usize;
    let mut data_table: &[u8] = &[];

    if len >= 8 {
        let magic = [data[0], data[1], data[2]];
        if magic == [0x62, 0x63, 0x78] {
            let _data_count = data[3];
            let data_table_size = read_u32_le(data, 4).unwrap_or(0) as usize;
            let start = 8 + data_table_size;
            if start <= len {
                data_table = &data[8..8 + data_table_size];
                code_start = start;
            } else {
                eprintln!(
                    "file truncated: declared data_table_size={} but file too small",
                    data_table_size
                );
                return;
            }
        }
    }

    let code = &data[code_start..];

    let mut jump_targets = BTreeSet::new();
    let mut data_refs = BTreeMap::new();
    let mut call_frames = BTreeMap::new();
    {
        let mut i = 0usize;
        while i < code.len() {
            match scan_one(code, i, &mut jump_targets, &mut data_refs, &mut call_frames) {
                Some(next) => i = next,
                None => break,
            }
        }
    }

    let data_entries = reconstruct_data(data_table, &data_refs);

    let _ = writeln!(w, "%asm");
    emit_data_section(w, data_table, &data_entries);
    let _ = writeln!(w, "%main");

    let mut i = 0usize;
    while i < code.len() {
        let abs_offset = code_start + i;
        match emit_instruction(
            w,
            code,
            i,
            abs_offset,
            &jump_targets,
            &data_entries,
            &call_frames,
        ) {
            Some(next) => i = next,
            None => break,
        }
    }
}

fn main() {
    let args: Vec<String> = env::args().collect();

    let mut dump = false;
    let mut decomp_flag = false;
    let mut path = String::from("out.bcx");
    let mut output_path: Option<String> = None;

    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--dump" => dump = true,
            "--decomp" => decomp_flag = true,
            "-o" | "--output" => {
                i += 1;
                if i < args.len() {
                    output_path = Some(args[i].clone());
                } else {
                    eprintln!("-o requires an output path");
                    return;
                }
            }
            other => path = other.to_string(),
        }
        i += 1;
    }

    if dump || decomp_flag {
        let mut f = match File::open(&path) {
            Ok(f) => f,
            Err(e) => {
                eprintln!("failed to open {}: {}", path, e);
                return;
            }
        };
        let mut data = Vec::new();
        if let Err(e) = f.read_to_end(&mut data) {
            eprintln!("failed to read {}: {}", path, e);
            return;
        }

        if decomp_flag {
            if let Some(ref out_path) = output_path {
                let out_file = match File::create(out_path) {
                    Ok(f) => f,
                    Err(e) => {
                        eprintln!("failed to create {}: {}", out_path, e);
                        return;
                    }
                };
                let mut w = BufWriter::new(out_file);
                decomp(&data, &mut w);
                let _ = w.flush();
            } else {
                let stdout = io::stdout();
                let mut w = BufWriter::new(stdout.lock());
                decomp(&data, &mut w);
                let _ = w.flush();
            }
            return;
        }

        let mut i = 0usize;
        let len = data.len();

        if len >= 8 {
            let magic = [data[0], data[1], data[2]];
            if magic == [0x62, 0x63, 0x78] {
                let data_count = data[3];
                let data_table_size = read_u32_le(&data, 4).unwrap_or(0);
                println!(
                    "Header: MAGIC=bcx data_count={} data_table_size={}",
                    data_count, data_table_size
                );
                let start = 8usize + (data_table_size as usize);
                if start <= len {
                    i = start;
                } else {
                    eprintln!(
                        "file truncated: declared data_table_size={} but file too small",
                        data_table_size
                    );
                    return;
                }
            }
        }

        while i < len {
            let offset = i;
            let opcode = data[i];
            i += 1;

            match opcode {
                opcodes::OPCODE_WRITE => {
                    let fd = { data[i] };
                    let slen = data[i + 1] as usize;
                    i += 2;
                    let s = if i + slen <= len {
                        String::from_utf8_lossy(&data[i..i + slen]).to_string()
                    } else {
                        "<truncated>".to_string()
                    };
                    i += slen.min(len - i);
                    println!("{:#06x}: write fd={} \"{}\"", offset, fd, s);
                }
                opcodes::OPCODE_NEWLINE => println!("{:#06x}: newline", offset),
                opcodes::OPCODE_PRINT => {
                    let ch = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: print '{}' (0x{:02x})", offset, ch as char, ch);
                }
                opcodes::OPCODE_PUSHI => {
                    if let Some(v) = read_i32_le(&data, i) {
                        println!("{:#06x}: pushi {}", offset, v);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: pushi <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_PUSH_REG => {
                    let r = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: push_reg R{}", offset, r);
                }
                opcodes::OPCODE_POP => {
                    let r = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: pop R{}", offset, r);
                }
                opcodes::OPCODE_MOVI => {
                    let dst = data.get(i).copied().unwrap_or(0);
                    if let Some(v) = read_i32_le(&data, i + 1) {
                        println!("{:#06x}: movi R{}, {}", offset, dst, v);
                        i += 1 + 4.min(len - (i + 1));
                    } else {
                        println!("{:#06x}: movi <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_MOV_REG => {
                    let dst = data.get(i).copied().unwrap_or(0);
                    let src = data.get(i + 1).copied().unwrap_or(0);
                    i += 2.min(len - i);
                    println!("{:#06x}: mov_reg R{}, R{}", offset, dst, src);
                }
                opcodes::OPCODE_ADD
                | opcodes::OPCODE_SUB
                | opcodes::OPCODE_MUL
                | opcodes::OPCODE_DIV
                | opcodes::OPCODE_XOR
                | opcodes::OPCODE_AND
                | opcodes::OPCODE_OR => {
                    let op = match opcode {
                        x if x == opcodes::OPCODE_ADD => "add",
                        x if x == opcodes::OPCODE_SUB => "sub",
                        x if x == opcodes::OPCODE_MUL => "mul",
                        x if x == opcodes::OPCODE_DIV => "div",
                        x if x == opcodes::OPCODE_XOR => "xor",
                        x if x == opcodes::OPCODE_AND => "and",
                        x if x == opcodes::OPCODE_OR => "or",
                        _ => "op",
                    };
                    let dst = data.get(i).copied().unwrap_or(0);
                    let src = data.get(i + 1).copied().unwrap_or(0);
                    i += 2.min(len - i);
                    println!("{:#06x}: {} R{}, R{}", offset, op, dst, src);
                }
                opcodes::OPCODE_PRINTREG => {
                    let r = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: printreg R{}", offset, r);
                }
                opcodes::OPCODE_JMP => {
                    if let Some(addr) = read_u32_le(&data, i) {
                        println!("{:#06x}: jmp 0x{:08x}", offset, addr);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: jmp <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_JE | opcodes::OPCODE_JNE => {
                    if let Some(addr) = read_u32_le(&data, i) {
                        let name = if opcode == opcodes::OPCODE_JE {
                            "je"
                        } else {
                            "jne"
                        };
                        println!("{:#06x}: {} 0x{:08x}", offset, name, addr);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: je/jne <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_INC => {
                    let r = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: inc R{}", offset, r);
                }
                opcodes::OPCODE_DEC => {
                    let r = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: dec R{}", offset, r);
                }
                opcodes::OPCODE_CMP => {
                    let a = data.get(i).copied().unwrap_or(0);
                    let b = data.get(i + 1).copied().unwrap_or(0);
                    i += 2.min(len - i);
                    println!("{:#06x}: cmp R{}, R{}", offset, a, b);
                }
                opcodes::OPCODE_ALLOC
                | opcodes::OPCODE_GROW
                | opcodes::OPCODE_RESIZE
                | opcodes::OPCODE_FREE
                | opcodes::OPCODE_SLEEP => {
                    if let Some(cnt) = read_u32_le(&data, i) {
                        let name = match opcode {
                            x if x == opcodes::OPCODE_ALLOC => "alloc",
                            x if x == opcodes::OPCODE_GROW => "grow",
                            x if x == opcodes::OPCODE_RESIZE => "resize",
                            x if x == opcodes::OPCODE_FREE => "free",
                            x if x == opcodes::OPCODE_SLEEP => "sleep",
                            _ => "num",
                        };
                        println!("{:#06x}: {} {}", offset, name, cnt);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_LOAD | opcodes::OPCODE_STORE => {
                    let reg = data.get(i).copied().unwrap_or(0);
                    if let Some(idx) = read_u32_le(&data, i + 1) {
                        let name = if opcode == opcodes::OPCODE_LOAD {
                            "load"
                        } else {
                            "store"
                        };
                        println!("{:#06x}: {} R{}, {}", offset, name, reg, idx);
                        i += 1 + 4.min(len - (i + 1));
                    } else {
                        println!(
                            "{:#06x}: {} <truncated>",
                            offset,
                            if opcode == opcodes::OPCODE_LOAD {
                                "load"
                            } else {
                                "store"
                            }
                        );
                        break;
                    }
                }
                opcodes::OPCODE_LOAD_REG | opcodes::OPCODE_STORE_REG => {
                    let a = data.get(i).copied().unwrap_or(0);
                    let b = data.get(i + 1).copied().unwrap_or(0);
                    i += 2.min(len - i);
                    let name = if opcode == opcodes::OPCODE_LOAD_REG {
                        "load_reg"
                    } else {
                        "store_reg"
                    };
                    println!("{:#06x}: {} r{}, r{}", offset, name, a, b);
                }
                opcodes::OPCODE_FOPEN => {
                    if i + 2 <= len {
                        let fd = data[i];
                        let nlen = data[i + 1] as usize;
                        i += 2;
                        let name = if i + nlen <= len {
                            String::from_utf8_lossy(&data[i..i + nlen]).to_string()
                        } else {
                            "<truncated>".to_string()
                        };
                        i += nlen.min(len - i);
                        println!("{:#06x}: fopen fd={} \"{}\"", offset, fd, name);
                    } else {
                        println!("{:#06x}: FOPEN <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_FCLOSE => {
                    let fd = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: fclose fd={}", offset, fd);
                }
                opcodes::OPCODE_FREAD => {
                    let fd = data.get(i).copied().unwrap_or(0);
                    let reg = data.get(i + 1).copied().unwrap_or(0);
                    i += 2.min(len - i);
                    println!("{:#06x}: fread fd={}, R{}", offset, fd, reg);
                }
                opcodes::OPCODE_FWRITE_REG => {
                    let fd = data.get(i).copied().unwrap_or(0);
                    let reg = data.get(i + 1).copied().unwrap_or(0);
                    i += 2.min(len - i);
                    println!("{:#06x}: fwrite_reg fd={}, R{}", offset, fd, reg);
                }
                opcodes::OPCODE_FWRITE_IMM => {
                    let fd = data.get(i).copied().unwrap_or(0);
                    if let Some(v) = read_i32_le(&data, i + 1) {
                        println!("{:#06x}: fwrite_imm fd={}, {}", offset, fd, v);
                        i += 1 + 4.min(len - (i + 1));
                    } else {
                        println!("{:#06x}: fwrite_imm <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_LOADSTR => {
                    if let Some(idx) = read_u32_le(&data, i) {
                        println!("{:#06x}: loadstr R{}, idx={} (data)", offset, 0, idx);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: loadstr <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_PRINTSTR => {
                    let r = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: printstr R{}", offset, r);
                }
                opcodes::OPCODE_READ => {
                    let r = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: read R{}", offset, r);
                }
                opcodes::OPCODE_CLRSCR => println!("{:#06x}: clrscr", offset),
                opcodes::OPCODE_RAND => {
                    let reg = data.get(i).copied().unwrap_or(0);

                    let min = if i + 1 + 8 <= len {
                        let mut arr = [0u8; 8];
                        arr.copy_from_slice(&data[i + 1..i + 9]);
                        u64::from_le_bytes(arr)
                    } else {
                        0
                    };
                    let max = if i + 9 + 8 <= len {
                        let mut arr = [0u8; 8];
                        arr.copy_from_slice(&data[i + 9..i + 17]);
                        u64::from_le_bytes(arr)
                    } else {
                        0
                    };
                    println!("{:#06x}: rand R{}, min={}, max={}", offset, reg, min, max);
                    i += 1 + 8 + 8;
                }
                opcodes::OPCODE_GETKEY => {
                    let r = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: getkey R{}", offset, r);
                }
                opcodes::OPCODE_CONTINUE => println!("{:#06x}: continue", offset),
                opcodes::OPCODE_READCHAR => {
                    let r = data.get(i).copied().unwrap_or(0);
                    i += 1.min(len - i);
                    println!("{:#06x}: readchar R{}", offset, r);
                }
                opcodes::OPCODE_JL => {
                    if let Some(addr) = read_u32_le(&data, i) {
                        println!("{:#06x}: jl 0x{:08x}", offset, addr);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: jl <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_JGE => {
                    if let Some(addr) = read_u32_le(&data, i) {
                        println!("{:#06x}: jge 0x{:08x}", offset, addr);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: jge <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_JB => {
                    if let Some(addr) = read_u32_le(&data, i) {
                        println!("{:#06x}: jb 0x{:08x}", offset, addr);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: jb <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_JAE => {
                    if let Some(addr) = read_u32_le(&data, i) {
                        println!("{:#06x}: jae 0x{:08x}", offset, addr);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: jae <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_CALL => {
                    if let Some(addr) = read_u32_le(&data, i) {
                        if let Some(frame) = read_u32_le(&data, i + 4) {
                            println!("{:#06x}: call 0x{:08x} frame={}", offset, addr, frame);
                            i += 8.min(len - i);
                        } else {
                            println!("{:#06x}: call <truncated>", offset);
                            break;
                        }
                    } else {
                        println!("{:#06x}: call <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_RET => println!("{:#06x}: ret", offset),
                opcodes::OPCODE_LOADBYTE
                | opcodes::OPCODE_LOADWORD
                | opcodes::OPCODE_LOADDWORD
                | opcodes::OPCODE_LOADQWORD => {
                    let r = data.get(i).copied().unwrap_or(0);
                    if let Some(idx) = read_u32_le(&data, i + 1) {
                        let name = match opcode {
                            x if x == opcodes::OPCODE_LOADBYTE => "loadbyte",
                            x if x == opcodes::OPCODE_LOADWORD => "loadword",
                            x if x == opcodes::OPCODE_LOADDWORD => "loaddword",
                            x if x == opcodes::OPCODE_LOADQWORD => "loadqword",
                            _ => "load*",
                        };
                        println!("{:#06x}: {} R{}, {}", offset, name, r, idx);
                        i += 1 + 4.min(len - (i + 1));
                    } else {
                        println!("{:#06x}: load* <truncated>", offset);
                        break;
                    }
                }
                opcodes::OPCODE_MOD => {
                    let dst = data.get(i).copied().unwrap_or(0);
                    let src = data.get(i + 1).copied().unwrap_or(0);
                    i += 2.min(len - i);
                    println!("{:#06x}: mod R{}, R{}", offset, dst, src);
                }
                opcodes::OPCODE_HALT => {
                    if i < len {
                        let code = data.get(i).copied().unwrap_or(0);
                        i += 1.min(len - i);
                        match code {
                            0 => println!("{:#06x}: halt ok ({})", offset, code),
                            1 => println!("{:#06x}: halt bad ({})", offset, code),
                            _ => println!("{:#06x}: halt {}", offset, code),
                        }
                    } else {
                        println!("{:#06x}: halt", offset);
                    }
                }
                opcodes::OPCODE_BREAK => println!("{:#06x}: break", offset),
                opcodes::OPCODE_JMPI => {
                    if let Some(addr) = read_u32_le(&data, i) {
                        println!("{:#06x}: jmpi 0x{:08x}", offset, addr);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: jmpi <truncated>", offset);
                        break;
                    }
                }
                other => {
                    println!("{:#06x}: UNKNOWN 0x{:02x}", offset, other);
                    println!("opcode = {}", other);
                }
            }
        }
    } else {
        eprintln!(
            "Usage: {} [--dump|--decomp] [file]",
            args.get(0).map(|s| s.as_str()).unwrap_or("program")
        );
    }
}
