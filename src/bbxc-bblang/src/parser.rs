use crate::ast::{Function, Program, Statement};
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
}

impl<'a> Parser<'a> {
    pub fn new(mut lexer: Lexer<'a>) -> Self {
        let cur = lexer.next_token();
        Parser {
            lexer,
            cur,
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
                Token::Unsafe => {
                    let asm = self.parse_unsafe_asm()?;
                    body.push(Statement::UnsafeAsm(asm));
                }
                other => {
                    return Err(ParseError(format!(
                        "Unsupported token in body (expected `unsafe asm {{ ... }}`): {:?}",
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

    fn parse_unsafe_asm(&mut self) -> Result<Vec<String>, ParseError> {
        self.expect(Token::Unsafe)?;
        self.expect(Token::Asm)?;

        if self.cur != Token::LBrace {
            return Err(ParseError(format!(
                "Expected '{{' after `unsafe asm`, found {:?}",
                self.cur
            )));
        }

        self.lexer.set_raw_mode(true);
        self.advance();

        let mut lines = Vec::new();
        while self.cur != Token::RBrace && self.cur != Token::Eof {
            match &self.cur {
                Token::RawLine(line) => {
                    lines.push(line.clone());
                    self.advance();
                }
                other => {
                    return Err(ParseError(format!(
                        "Unsupported token in `unsafe asm` body: {:?}",
                        other
                    )));
                }
            }
        }

        self.lexer.set_raw_mode(false);
        self.expect(Token::RBrace)?;
        Ok(lines)
    }
}
