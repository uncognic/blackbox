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

    let mut dump = false;
    let mut decomp = false;
    let mut path = String::from("out.bcx");

    for a in args.iter().skip(1) {
        match a.as_str() {
            "--dump" => dump = true,
            "--decomp" => decomp = true,
            other => path = other.to_string(),
        }
    }

    if dump {
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
                opcodes::OPCODE_PUSH_IMM => {
                    if let Some(v) = read_i32_le(&data, i) {
                        println!("{:#06x}: push_imm {}", offset, v);
                        i += 4.min(len - i);
                    } else {
                        println!("{:#06x}: push_imm <truncated>", offset);
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

                other => {
                    println!("{:#06x}: UNKNOWN 0x{:02x}", offset, other);
                    println!("opcode = {}", other);
                }
            }
        }
    } else if decomp {
        println!("--decomp selected: decomp mode not implemented yet");
    } else {
        eprintln!(
            "Usage: {} [--dump|--decomp] [file]",
            args.get(0).map(|s| s.as_str()).unwrap_or("program")
        );
    }
}
