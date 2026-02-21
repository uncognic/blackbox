#[derive(Debug, PartialEq)]
pub struct Program {
    pub functions: Vec<Function>,
}

#[derive(Debug, PartialEq)]
pub struct Function {
    pub name: String,
    pub params: Vec<String>,
    pub body: Vec<Statement>,
}

#[derive(Debug, PartialEq)]
pub enum Statement {
    Empty,
    Instr(Instruction),
    UnsafeBlock(Vec<Statement>),
}

#[derive(Debug, PartialEq)]
pub struct Instruction {
    pub name: String,
    pub args: Vec<Operand>,
}

#[derive(Debug, PartialEq)]
pub enum Operand {
    Imm(i64),
    Reg(u8),
    Str(String),
    Char(u8),
    Ident(String),
}

impl Program {
    pub fn new() -> Self {
        Program {
            functions: Vec::new(),
        }
    }
}
