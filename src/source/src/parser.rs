use crate::ast::{Function, Instruction, Operand, Program, Statement};
use crate::lexer::{Lexer, Token};
use std::fmt;

#[derive(Debug)]
pub struct ParseError(pub String);

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl std::error::Error for ParseError {}

pub struct Parser<'a> {
    lexer: Lexer<'a>,
    cur: Token,
    in_unsafe: bool,
}

impl<'a> Parser<'a> {
    pub fn new(mut lexer: Lexer<'a>) -> Self {
        let cur = lexer.next_token();
        Parser {
            lexer,
            cur,
            in_unsafe: false,
        }
    }

    fn advance(&mut self) {
        self.cur = self.lexer.next_token();
    }

    fn expect(&mut self, expected: Token) -> Result<(), ParseError> {
        if std::mem::discriminant(&self.cur) == std::mem::discriminant(&expected) {
            self.advance();
            Ok(())
        } else {
            Err(ParseError(format!(
                "Expected {:?}, found {:?}",
                expected, self.cur
            )))
        }
    }

    pub fn parse_program(&mut self) -> Result<Program, ParseError> {
        let mut prog = Program::new();
        while self.cur != Token::Eof {
            let func = self.parse_function()?;
            prog.functions.push(func);
        }
        Ok(prog)
    }

    fn parse_function(&mut self) -> Result<Function, ParseError> {
        if self.cur != Token::Fn {
            return Err(ParseError(format!(
                "Expected 'fn' keyword, found: {:?}",
                self.cur
            )));
        }
        self.advance();

        let name = match &self.cur {
            Token::Ident(s) => s.clone(),
            other => {
                return Err(ParseError(format!(
                    "Expected identifier after fn, found: {:?}",
                    other
                )))
            }
        };
        if name != "main" {
            return Err(ParseError(format!(
                "Only `main` is supported, found function '{}'",
                name
            )));
        }
        self.advance();

        self.expect(Token::LParen)?;
        self.expect(Token::RParen)?;

        self.expect(Token::LBrace)?;

        let mut body = Vec::new();
        while self.cur != Token::RBrace && self.cur != Token::Eof {
            match &self.cur {
                Token::Semicolon => {
                    self.advance();
                    body.push(Statement::Empty);
                }
                Token::Ident(id) if id == "unsafe" => {
                    // parse unsafe { ... }
                    self.advance();
                    self.expect(Token::LBrace)?;
                    let prev = self.in_unsafe;
                    self.in_unsafe = true;
                    let mut inner: Vec<Statement> = Vec::new();
                    while self.cur != Token::RBrace && self.cur != Token::Eof {
                        match &self.cur {
                            Token::Semicolon => {
                                self.advance();
                                inner.push(Statement::Empty);
                            }
                            Token::Ident(_) => {
                                let instr = self.parse_instruction()?;
                                inner.push(Statement::Instr(instr));
                            }
                            other => {
                                return Err(ParseError(format!(
                                    "Unsupported token in unsafe body: {:?}",
                                    other
                                )))
                            }
                        }
                    }
                    self.expect(Token::RBrace)?;
                    self.in_unsafe = prev;
                    body.push(Statement::UnsafeBlock(inner));
                }
                Token::Ident(_) => {
                    let instr = self.parse_instruction()?;
                    body.push(Statement::Instr(instr));
                }
                other => {
                    return Err(ParseError(format!(
                        "Unsupported token in body: {:?}",
                        other
                    )))
                }
            }
        }

        self.expect(Token::RBrace)?;

        Ok(Function {
            name,
            params: Vec::new(),
            body,
        })
    }

    fn parse_instruction(&mut self) -> Result<Instruction, ParseError> {
        let name = match &self.cur {
            Token::Ident(s) => s.clone(),
            other => {
                return Err(ParseError(format!(
                    "Expected instruction name, found {:?}",
                    other
                )))
            }
        };
        self.advance();

        let mut args = Vec::new();
        if self.cur == Token::LParen {
            self.advance();
            while self.cur != Token::RParen {
                let operand = match &self.cur {
                    Token::Number(n) => {
                        let v = *n;
                        self.advance();
                        Operand::Imm(v)
                    }
                    Token::Str(s) => {
                        let s2 = s.clone();
                        self.advance();
                        Operand::Str(s2)
                    }
                    Token::Char(c) => {
                        let c2 = *c as u8;
                        self.advance();
                        Operand::Char(c2)
                    }
                    Token::Ident(id) => {
                        if id.len() > 0
                            && (id.chars().next().unwrap() == 'R'
                                || id.chars().next().unwrap() == 'r')
                        {
                            if !self.in_unsafe {
                                return Err(ParseError(format!(
                                    "Register {} used outside unsafe block",
                                    id
                                )));
                            }
                            let num = id[1..].parse::<u8>().unwrap_or(0);
                            self.advance();
                            Operand::Reg(num)
                        } else {
                            let s2 = id.clone();
                            self.advance();
                            Operand::Ident(s2)
                        }
                    }
                    other => return Err(ParseError(format!("Unexpected operand: {:?}", other))),
                };
                args.push(operand);
                if self.cur == Token::Comma {
                    self.advance();
                    continue;
                } else if self.cur == Token::RParen {
                    break;
                } else {
                    return Err(ParseError(format!(
                        "Expected ',' or ')', found {:?}",
                        self.cur
                    )));
                }
            }
            self.expect(Token::RParen)?;
        }

        if self.cur == Token::Semicolon {
            self.advance();
        }

        Ok(Instruction { name, args })
    }
}
