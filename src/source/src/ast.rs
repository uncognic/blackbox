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
    UnsafeAsm(Vec<String>),
}

impl Program {
    pub fn new() -> Self {
        Program {
            functions: Vec::new(),
        }
    }
}
