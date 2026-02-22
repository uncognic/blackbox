use std::env;
use std::fs::File;
use std::io::Read;

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

fn main() {
    let args: Vec<String> = env::args().collect();
    let path = if args.len() > 1 { &args[1] } else { "out.bcx" };

    let mut f = match File::open(path) {
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
                println!("{:#06x}: WRITE fd={} \"{}\"", offset, fd, s);
            }
            opcodes::OPCODE_NEWLINE => println!("{:#06x}: NEWLINE", offset),
            opcodes::OPCODE_PRINT => {
                let ch = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: PRINT '{}' (0x{:02x})", offset, ch as char, ch);
            }
            opcodes::OPCODE_PUSH_IMM => {
                if let Some(v) = read_i32_le(&data, i) {
                    println!("{:#06x}: PUSH_IMM {}", offset, v);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: PUSH_IMM <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_PUSH_REG => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: PUSH_REG r{}", offset, r);
            }
            opcodes::OPCODE_POP => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: POP r{}", offset, r);
            }
            opcodes::OPCODE_MOV_IMM => {
                let dst = data.get(i).copied().unwrap_or(0);
                if let Some(v) = read_i32_le(&data, i + 1) {
                    println!("{:#06x}: MOV_IMM r{} {}", offset, dst, v);
                    i += 1 + 4.min(len - (i + 1));
                } else {
                    println!("{:#06x}: MOV_IMM <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_MOV_REG => {
                let dst = data.get(i).copied().unwrap_or(0);
                let src = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: MOV_REG r{}, r{}", offset, dst, src);
            }
            opcodes::OPCODE_ADD
            | opcodes::OPCODE_SUB
            | opcodes::OPCODE_MUL
            | opcodes::OPCODE_DIV
            | opcodes::OPCODE_XOR
            | opcodes::OPCODE_AND
            | opcodes::OPCODE_OR => {
                let op = match opcode {
                    x if x == opcodes::OPCODE_ADD => "ADD",
                    x if x == opcodes::OPCODE_SUB => "SUB",
                    x if x == opcodes::OPCODE_MUL => "MUL",
                    x if x == opcodes::OPCODE_DIV => "DIV",
                    x if x == opcodes::OPCODE_XOR => "XOR",
                    x if x == opcodes::OPCODE_AND => "AND",
                    x if x == opcodes::OPCODE_OR => "OR",
                    _ => "OP",
                };
                let dst = data.get(i).copied().unwrap_or(0);
                let src = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: {} r{}, r{}", offset, op, dst, src);
            }
            opcodes::OPCODE_PRINTREG => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: PRINTREG r{}", offset, r);
            }
            opcodes::OPCODE_JMP => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JMP 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JMP <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_JE | opcodes::OPCODE_JNE => {
                if let Some(addr) = read_u32_le(&data, i) {
                    let name = if opcode == opcodes::OPCODE_JE {
                        "JE"
                    } else {
                        "JNE"
                    };
                    println!("{:#06x}: {} 0x{:08x}", offset, name, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JE/JNE <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_INC => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: INC r{}", offset, r);
            }
            opcodes::OPCODE_DEC => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: DEC r{}", offset, r);
            }
            opcodes::OPCODE_CMP => {
                let a = data.get(i).copied().unwrap_or(0);
                let b = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: CMP r{}, r{}", offset, a, b);
            }
            opcodes::OPCODE_ALLOC
            | opcodes::OPCODE_GROW
            | opcodes::OPCODE_RESIZE
            | opcodes::OPCODE_FREE
            | opcodes::OPCODE_SLEEP => {
                if let Some(cnt) = read_u32_le(&data, i) {
                    let name = match opcode {
                        x if x == opcodes::OPCODE_ALLOC => "ALLOC",
                        x if x == opcodes::OPCODE_GROW => "GROW",
                        x if x == opcodes::OPCODE_RESIZE => "RESIZE",
                        x if x == opcodes::OPCODE_FREE => "FREE",
                        x if x == opcodes::OPCODE_SLEEP => "SLEEP",
                        _ => "NUM",
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
                        "LOAD"
                    } else {
                        "STORE"
                    };
                    println!("{:#06x}: {} r{}, {}", offset, name, reg, idx);
                    i += 1 + 4.min(len - (i + 1));
                } else {
                    println!(
                        "{:#06x}: {} <truncated>",
                        offset,
                        if opcode == opcodes::OPCODE_LOAD {
                            "LOAD"
                        } else {
                            "STORE"
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
                    "LOAD_REG"
                } else {
                    "STORE_REG"
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
                    println!("{:#06x}: FOPEN fd={} \"{}\"", offset, fd, name);
                } else {
                    println!("{:#06x}: FOPEN <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_FCLOSE => {
                let fd = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: FCLOSE fd={}", offset, fd);
            }
            opcodes::OPCODE_FREAD => {
                let fd = data.get(i).copied().unwrap_or(0);
                let reg = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: FREAD fd={}, r{}", offset, fd, reg);
            }
            opcodes::OPCODE_FWRITE_REG => {
                let fd = data.get(i).copied().unwrap_or(0);
                let reg = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: FWRITE_REG fd={}, r{}", offset, fd, reg);
            }
            opcodes::OPCODE_FWRITE_IMM => {
                let fd = data.get(i).copied().unwrap_or(0);
                if let Some(v) = read_i32_le(&data, i + 1) {
                    println!("{:#06x}: FWRITE_IMM fd={}, {}", offset, fd, v);
                    i += 1 + 4.min(len - (i + 1));
                } else {
                    println!("{:#06x}: FWRITE_IMM <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_LOADSTR => {
                if let Some(idx) = read_u32_le(&data, i) {
                    println!("{:#06x}: LOADSTR r{}, idx={} (data)", offset, 0, idx);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: LOADSTR <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_PRINTSTR => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: PRINTSTR r{}", offset, r);
            }
            opcodes::OPCODE_READ => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: READ r{}", offset, r);
            }
            opcodes::OPCODE_CLRSCR => println!("{:#06x}: CLRSCR", offset),
            opcodes::OPCODE_RAND => println!("{:#06x}: RAND", offset),
            opcodes::OPCODE_GETKEY => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: GETKEY r{}", offset, r);
            }
            opcodes::OPCODE_CONTINUE => println!("{:#06x}: CONTINUE", offset),
            opcodes::OPCODE_READCHAR => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: READCHAR r{}", offset, r);
            }
            opcodes::OPCODE_JL => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JL 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JL <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_JGE => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JGE 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JGE <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_JB => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JB 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JB <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_JAE => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JAE 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JAE <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_CALL => {
                if let Some(addr) = read_u32_le(&data, i) {
                    if let Some(frame) = read_u32_le(&data, i + 4) {
                        println!("{:#06x}: CALL 0x{:08x} frame={}", offset, addr, frame);
                        i += 8.min(len - i);
                    } else {
                        println!("{:#06x}: CALL <truncated>", offset);
                        break;
                    }
                } else {
                    println!("{:#06x}: CALL <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_RET => println!("{:#06x}: RET", offset),
            opcodes::OPCODE_LOADBYTE
            | opcodes::OPCODE_LOADWORD
            | opcodes::OPCODE_LOADDWORD
            | opcodes::OPCODE_LOADQWORD => {
                let r = data.get(i).copied().unwrap_or(0);
                if let Some(idx) = read_u32_le(&data, i + 1) {
                    let name = match opcode {
                        x if x == opcodes::OPCODE_LOADBYTE => "LOADBYTE",
                        x if x == opcodes::OPCODE_LOADWORD => "LOADWORD",
                        x if x == opcodes::OPCODE_LOADDWORD => "LOADDWORD",
                        x if x == opcodes::OPCODE_LOADQWORD => "LOADQWORD",
                        _ => "LOAD*",
                    };
                    println!("{:#06x}: {} r{}, {}", offset, name, r, idx);
                    i += 1 + 4.min(len - (i + 1));
                } else {
                    println!("{:#06x}: LOAD* <truncated>", offset);
                    break;
                }
            }
            opcodes::OPCODE_MOD => {
                let dst = data.get(i).copied().unwrap_or(0);
                let src = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: MOD r{}, r{}", offset, dst, src);
            }
            opcodes::OPCODE_HALT => {
                println!("{:#06x}: HALT", offset);
            }
            other => {
                println!("{:#06x}: UNKNOWN 0x{:02x}", offset, other);
            }
        }
    }
}
