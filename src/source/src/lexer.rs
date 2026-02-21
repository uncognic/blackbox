use std::iter::Peekable;
use std::str::Chars;

#[derive(Debug, PartialEq, Clone)]
pub enum Token {
    Fn,
    Ident(String),
    Number(i64),
    Str(String),
    Char(char),
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Semicolon,
    Eof,
}

pub struct Lexer<'a> {
    input: Peekable<Chars<'a>>,
}

impl<'a> Lexer<'a> {
    pub fn new(src: &'a str) -> Self {
        Lexer {
            input: src.chars().peekable(),
        }
    }

    fn read_identifier(&mut self, first: char) -> String {
        let mut s = String::new();
        s.push(first);
        while let Some(&ch) = self.input.peek() {
            if ch.is_alphanumeric() || ch == '_' {
                s.push(ch);
                self.input.next();
            } else {
                break;
            }
        }
        s
    }

    fn read_number(&mut self, first: char) -> i64 {
        let mut s = String::new();
        s.push(first);
        while let Some(&ch) = self.input.peek() {
            if ch.is_ascii_hexdigit() || ch.is_ascii_digit() || ch == 'x' || ch == 'X' {
                s.push(ch);
                self.input.next();
            } else {
                break;
            }
        }
        if s.starts_with("0x") || s.starts_with("0X") {
            i64::from_str_radix(&s[2..], 16).unwrap_or(0)
        } else {
            s.parse::<i64>().unwrap_or(0)
        }
    }

    fn read_string(&mut self) -> String {
        let mut s = String::new();
        while let Some(ch) = self.input.next() {
            match ch {
                '\\' => {
                    if let Some(esc) = self.input.next() {
                        s.push(match esc {
                            'n' => '\n',
                            'r' => '\r',
                            't' => '\t',
                            '\\' => '\\',
                            '"' => '"',
                            other => other,
                        });
                    }
                }
                '"' => break,
                other => s.push(other),
            }
        }
        s
    }

    fn skip_whitespace(&mut self) {
        while let Some(&ch) = self.input.peek() {
            if ch.is_whitespace() {
                self.input.next();
            } else {
                break;
            }
        }
    }

    pub fn next_token(&mut self) -> Token {
        self.skip_whitespace();
        match self.input.next() {
            Some('(') => Token::LParen,
            Some(')') => Token::RParen,
            Some('{') => Token::LBrace,
            Some('}') => Token::RBrace,
            Some(',') => Token::Comma,
            Some(';') => Token::Semicolon,
            Some('"') => {
                let s = self.read_string();
                Token::Str(s)
            }
            Some('\'') => {
                if let Some(c) = self.input.next() {
                    if let Some('\'') = self.input.next() {}
                    Token::Char(c)
                } else {
                    Token::Eof
                }
            }
            Some(ch) if ch.is_numeric() => {
                let n = self.read_number(ch);
                Token::Number(n)
            }
            Some(ch) if ch.is_alphabetic() || ch == '_' => {
                let ident = self.read_identifier(ch);
                match ident.as_str() {
                    "fn" => Token::Fn,
                    other => Token::Ident(other.to_string()),
                }
            }
            None => Token::Eof,
            Some(_) => self.next_token(),
        }
    }
}
