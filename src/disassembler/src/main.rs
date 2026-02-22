use std::env;
use std::fs::File;
use std::io::Read;

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
            0x01 => {
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
            0x02 => println!("{:#06x}: NEWLINE", offset),
            0x03 => {
                let ch = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: PRINT '{}' (0x{:02x})", offset, ch as char, ch);
            }
            0x04 => {
                if let Some(v) = read_i32_le(&data, i) {
                    println!("{:#06x}: PUSH_IMM {}", offset, v);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: PUSH_IMM <truncated>", offset);
                    break;
                }
            }
            0x12 => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: PUSH_REG r{}", offset, r);
            }
            0x05 => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: POP r{}", offset, r);
            }
            0x0B => {
                let dst = data.get(i).copied().unwrap_or(0);
                if let Some(v) = read_i32_le(&data, i + 1) {
                    println!("{:#06x}: MOV_IMM r{} {}", offset, dst, v);
                    i += 1 + 4.min(len - (i + 1));
                } else {
                    println!("{:#06x}: MOV_IMM <truncated>", offset);
                    break;
                }
            }
            0x0C => {
                let dst = data.get(i).copied().unwrap_or(0);
                let src = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: MOV_REG r{}, r{}", offset, dst, src);
            }
            0x06 | 0x07 | 0x08 | 0x09 | 0x2A | 0x2B | 0x2C => {
                let op = match opcode {
                    0x06 => "ADD",
                    0x07 => "SUB",
                    0x08 => "MUL",
                    0x09 => "DIV",
                    0x2A => "XOR",
                    0x2B => "AND",
                    0x2C => "OR",
                    _ => "OP",
                };
                let dst = data.get(i).copied().unwrap_or(0);
                let src = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: {} r{}, r{}", offset, op, dst, src);
            }
            0x0A => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: PRINTREG r{}", offset, r);
            }
            0x0D => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JMP 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JMP <truncated>", offset);
                    break;
                }
            }
            0x0E | 0x0F => {
                let reg = data.get(i).copied().unwrap_or(0);
                if let Some(addr) = read_u32_le(&data, i + 1) {
                    let name = if opcode == 0x0E { "JE" } else { "JNE" };
                    println!("{:#06x}: {} r{}, 0x{:08x}", offset, name, reg, addr);
                    i += 1 + 4.min(len - (i + 1));
                } else {
                    println!("{:#06x}: JE/JNE <truncated>", offset);
                    break;
                }
            }
            0x10 => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: INC r{}", offset, r);
            }
            0x11 => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: DEC r{}", offset, r);
            }
            0x13 => {
                let a = data.get(i).copied().unwrap_or(0);
                let b = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: CMP r{}, r{}", offset, a, b);
            }
            0x14 | 0x17 | 0x19 | 0x20 | 0x2F => {
                if let Some(cnt) = read_u32_le(&data, i) {
                    let name = match opcode {
                        0x14 => "ALLOC",
                        0x17 => "GROW",
                        0x19 => "RESIZE",
                        0x20 => "FREE",
                        0x2F => "SLEEP",
                        _ => "NUM",
                    };
                    println!("{:#06x}: {} {}", offset, name, cnt);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: <truncated>", offset);
                    break;
                }
            }
            0x15 | 0x16 => {
                let reg = data.get(i).copied().unwrap_or(0);
                if let Some(idx) = read_u32_le(&data, i + 1) {
                    let name = if opcode == 0x15 { "LOAD" } else { "STORE" };
                    println!("{:#06x}: {} r{}, {}", offset, name, reg, idx);
                    i += 1 + 4.min(len - (i + 1));
                } else {
                    println!(
                        "{:#06x}: {} <truncated>",
                        offset,
                        if opcode == 0x15 { "LOAD" } else { "STORE" }
                    );
                    break;
                }
            }
            0x41 | 0x42 => {
                let a = data.get(i).copied().unwrap_or(0);
                let b = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                let name = if opcode == 0x41 {
                    "LOAD_REG"
                } else {
                    "STORE_REG"
                };
                println!("{:#06x}: {} r{}, r{}", offset, name, a, b);
            }
            0x21 => {
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
            0x22 => {
                let fd = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: FCLOSE fd={}", offset, fd);
            }
            0x23 => {
                let fd = data.get(i).copied().unwrap_or(0);
                let reg = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: FREAD fd={}, r{}", offset, fd, reg);
            }
            0x24 => {
                let fd = data.get(i).copied().unwrap_or(0);
                let reg = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: FWRITE_REG fd={}, r{}", offset, fd, reg);
            }
            0x27 => {
                let fd = data.get(i).copied().unwrap_or(0);
                if let Some(v) = read_i32_le(&data, i + 1) {
                    println!("{:#06x}: FWRITE_IMM fd={}, {}", offset, fd, v);
                    i += 1 + 4.min(len - (i + 1));
                } else {
                    println!("{:#06x}: FWRITE_IMM <truncated>", offset);
                    break;
                }
            }
            0x28 => {
                if let Some(idx) = read_u32_le(&data, i) {
                    println!("{:#06x}: LOADSTR r{}, idx={} (data)", offset, 0, idx);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: LOADSTR <truncated>", offset);
                    break;
                }
            }
            0x29 => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: PRINTSTR r{}", offset, r);
            }
            0x33 => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: READ r{}", offset, r);
            }
            0x30 => println!("{:#06x}: CLRSCR", offset),
            0x31 => println!("{:#06x}: RAND", offset),
            0x32 => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: GETKEY r{}", offset, r);
            }
            0x34 => println!("{:#06x}: CONTINUE", offset),
            0x35 => {
                let r = data.get(i).copied().unwrap_or(0);
                i += 1.min(len - i);
                println!("{:#06x}: READCHAR r{}", offset, r);
            }
            0x36 => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JL 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JL <truncated>", offset);
                    break;
                }
            }
            0x37 => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JGE 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JGE <truncated>", offset);
                    break;
                }
            }
            0x38 => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JB 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JB <truncated>", offset);
                    break;
                }
            }
            0x39 => {
                if let Some(addr) = read_u32_le(&data, i) {
                    println!("{:#06x}: JAE 0x{:08x}", offset, addr);
                    i += 4.min(len - i);
                } else {
                    println!("{:#06x}: JAE <truncated>", offset);
                    break;
                }
            }
            0x3A => {
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
            0x3B => println!("{:#06x}: RET", offset),
            0x3C | 0x3D | 0x3E | 0x3F => {
                let r = data.get(i).copied().unwrap_or(0);
                if let Some(idx) = read_u32_le(&data, i + 1) {
                    let name = match opcode {
                        0x3C => "LOADBYTE",
                        0x3D => "LOADWORD",
                        0x3E => "LOADDWORD",
                        0x3F => "LOADQWORD",
                        _ => "LOAD*",
                    };
                    println!("{:#06x}: {} r{}, {}", offset, name, r, idx);
                    i += 1 + 4.min(len - (i + 1));
                } else {
                    println!("{:#06x}: LOAD* <truncated>", offset);
                    break;
                }
            }
            0x40 => {
                let dst = data.get(i).copied().unwrap_or(0);
                let src = data.get(i + 1).copied().unwrap_or(0);
                i += 2.min(len - i);
                println!("{:#06x}: MOD r{}, r{}", offset, dst, src);
            }
            0xFF => {
                println!("{:#06x}: HALT", offset);
            }
            other => {
                println!("{:#06x}: UNKNOWN 0x{:02x}", offset, other);
            }
        }
    }
}
