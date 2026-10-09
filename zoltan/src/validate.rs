//! MVP validator for Joltscript kernel sources.
//!
//! Mirrors the MVP subset accepted by the Glue Layer compiler
//! (`joltscript/layers/glue/src/compiler.c`): a single
//! `(defkernel name [inputs...] body)` form with scalar `f32` expressions,
//! at most 32 bindings, and nesting depth 64. When the Glue compiler grows,
//! this validator must grow with it; any divergence is a bug.
//!
//! Future home per `zoltan/AGENTS.md`: `src/core/validation.rs` with the
//! command driver in `src/commands/`.

use std::fmt;

/// A single validation failure with source position.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Diagnostic {
    pub line: usize,
    pub column: usize,
    pub message: String,
}

impl fmt::Display for Diagnostic {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}: {}", self.line, self.column, self.message)
    }
}

/// Facts extracted from a valid kernel source.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct KernelInfo {
    pub name: String,
    pub category: String,
    pub inputs: Vec<String>,
    pub outputs: u32,
}

#[derive(Debug, Clone)]
struct Token {
    text: String,
    line: usize,
    column: usize,
}

fn tokenize(source: &str) -> Result<Vec<Token>, Diagnostic> {
    let mut tokens = Vec::new();
    let mut line = 1usize;
    let mut column = 1usize;
    let bytes = source.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i] as char;
        if c == '\n' {
            line += 1;
            column = 1;
            i += 1;
        } else if c.is_whitespace() {
            column += 1;
            i += 1;
        } else if c == ';' {
            // `;` starts a comment through end of line (matches Glue compiler).
            while i < bytes.len() && bytes[i] as char != '\n' {
                i += 1;
                column += 1;
            }
        } else if "()[]".contains(c) {
            tokens.push(Token {
                text: c.to_string(),
                line,
                column,
            });
            column += 1;
            i += 1;
        } else {
            let start_col = column;
            let mut text = String::new();
            while i < bytes.len() {
                let d = bytes[i] as char;
                if d.is_whitespace() || "()[];".contains(d) {
                    break;
                }
                text.push(d);
                i += 1;
                column += 1;
            }
            tokens.push(Token {
                text,
                line,
                column: start_col,
            });
        }
    }
    // Delimiter balance with the position of the mismatch.
    let mut stack: Vec<&Token> = Vec::new();
    for token in &tokens {
        match token.text.as_str() {
            "(" | "[" => stack.push(token),
            ")" => match stack.pop() {
                Some(open) if open.text == "(" => {}
                Some(open) => {
                    return Err(Diagnostic {
                        line: token.line,
                        column: token.column,
                        message: format!("mismatched '{}' closed by ')'", open.text),
                    })
                }
                None => {
                    return Err(Diagnostic {
                        line: token.line,
                        column: token.column,
                        message: "unmatched ')'".to_string(),
                    })
                }
            },
            "]" => match stack.pop() {
                Some(open) if open.text == "[" => {}
                Some(open) => {
                    return Err(Diagnostic {
                        line: token.line,
                        column: token.column,
                        message: format!("mismatched '{}' closed by ']'", open.text),
                    })
                }
                None => {
                    return Err(Diagnostic {
                        line: token.line,
                        column: token.column,
                        message: "unmatched ']'".to_string(),
                    })
                }
            },
            _ => {}
        }
    }
    if let Some(open) = stack.pop() {
        return Err(Diagnostic {
            line: open.line,
            column: open.column,
            message: format!("unclosed '{}'", open.text),
        });
    }
    Ok(tokens)
}

fn is_binding(name: &str) -> bool {
    let mut chars = name.chars();
    match chars.next() {
        Some(c) if c.is_ascii_alphabetic() || c == '_' => {}
        _ => return false,
    }
    chars.all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-' || c == '?')
}

fn arity(op: &str) -> Option<usize> {
    match op {
        "+" | "-" | "*" | "/" | "min" | "max" | "pow" | "<" | ">" | "<=" | ">=" | "=" | "!="
        | "and" | "or" | "bitwise-and" | "bitwise-or" | "bitwise-xor" | "shl" | "shr" => Some(2),
        "abs" | "floor" | "sqrt" | "not" => Some(1),
        "select" => Some(3),
        _ => None,
    }
}

struct Parser<'a> {
    tokens: &'a [Token],
    pos: usize,
    bindings: &'a [String],
}

impl<'a> Parser<'a> {
    fn peek(&self) -> Option<&'a Token> {
        self.tokens.get(self.pos)
    }

    fn next(&mut self) -> Option<&'a Token> {
        let token = self.tokens.get(self.pos);
        if token.is_some() {
            self.pos += 1;
        }
        token
    }

    fn expect(&mut self, want: &str) -> Result<Token, Diagnostic> {
        match self.next() {
            Some(t) if t.text == want => Ok(t.clone()),
            Some(t) => Err(Diagnostic {
                line: t.line,
                column: t.column,
                message: format!("expected '{want}', found '{}'", t.text),
            }),
            None => Err(Diagnostic {
                line: 1,
                column: 1,
                message: format!("expected '{want}', found end of input"),
            }),
        }
    }

    fn expression(&mut self, depth: usize) -> Result<(), Diagnostic> {
        if depth >= 64 {
            let t = self.peek().cloned();
            let (line, column) = t.map(|t| (t.line, t.column)).unwrap_or((1, 1));
            return Err(Diagnostic {
                line,
                column,
                message: "expression nesting limit exceeded".to_string(),
            });
        }
        let token = self.next().cloned().ok_or(Diagnostic {
            line: 1,
            column: 1,
            message: "unexpected end of input in expression".to_string(),
        })?;
        if token.text == "(" {
            let head = self.next().cloned().ok_or(Diagnostic {
                line: token.line,
                column: token.column,
                message: "unexpected end of input after '('".to_string(),
            })?;
            let Some(expected) = arity(&head.text) else {
                return Err(Diagnostic {
                    line: head.line,
                    column: head.column,
                    message: format!("unknown operation '{}'", head.text),
                });
            };
            for _ in 0..expected {
                self.expression(depth + 1)?;
            }
            self.expect(")")?;
            Ok(())
        } else if ["(", ")", "[", "]"].contains(&token.text.as_str()) {
            Err(Diagnostic {
                line: token.line,
                column: token.column,
                message: format!("unexpected '{}' in expression", token.text),
            })
        } else if self.bindings.iter().any(|b| b == &token.text) {
            Ok(())
        } else {
            match token.text.parse::<f32>() {
                Ok(v) if v.is_finite() => Ok(()),
                _ => Err(Diagnostic {
                    line: token.line,
                    column: token.column,
                    message: format!(
                        "undefined binding or invalid finite f32 literal '{}'",
                        token.text
                    ),
                }),
            }
        }
    }
}

/// Validate a kernel source. Returns facts about the kernel on success.
pub fn validate_source(source: &str) -> Result<KernelInfo, Diagnostic> {
    let fail = |line: usize, column: usize, message: &str| Diagnostic {
        line,
        column,
        message: message.to_string(),
    };

    // Required metadata headers (`;; @key value`).
    let mut meta: Vec<(String, String, usize, usize)> = Vec::new();
    for (index, line) in source.lines().enumerate() {
        let trimmed = line.trim_start();
        let body = trimmed.strip_prefix(";;").unwrap_or("").trim_start();
        if let Some(rest) = body.strip_prefix('@') {
            let mut parts = rest.splitn(2, char::is_whitespace);
            let key = parts.next().unwrap_or("").to_string();
            let value = parts.next().unwrap_or("").trim().to_string();
            let column = line.len() - line.trim_start().len() + 4;
            meta.push((key, value, index + 1, column));
        }
    }
    let required = [
        "kernel",
        "category",
        "description",
        "complexity",
        "gpu",
        "since",
    ];
    let mut values = std::collections::HashMap::new();
    for (key, value, line, column) in &meta {
        if required.contains(&key.as_str()) {
            values.insert(key.clone(), (value.clone(), *line, *column));
        }
    }
    for key in required {
        if !values.contains_key(key) {
            return Err(fail(1, 1, &format!("missing required metadata '@{key}'")));
        }
    }
    let get = |key: &str| values[key].0.clone();
    match get("complexity").as_str() {
        "Low" | "Medium" | "High" => {}
        other => {
            let (_, line, column) = values["complexity"];
            return Err(fail(line, column, &format!("invalid complexity '{other}'")));
        }
    }
    match get("gpu").as_str() {
        "Yes" | "No" | "Partial" => {}
        other => {
            let (_, line, column) = values["gpu"];
            return Err(fail(line, column, &format!("invalid gpu '{other}'")));
        }
    }

    let tokens = tokenize(source)?;
    if tokens.is_empty() {
        return Err(fail(1, 1, "missing (defkernel ...) form"));
    }
    let mut parser = Parser {
        tokens: &tokens,
        pos: 0,
        bindings: &[],
    };
    parser.expect("(")?;
    let defkernel = parser
        .next()
        .cloned()
        .ok_or(fail(1, 1, "missing (defkernel ...) form"))?;
    if defkernel.text != "defkernel" {
        return Err(fail(defkernel.line, defkernel.column, "expected defkernel"));
    }
    let name = parser
        .next()
        .cloned()
        .ok_or(fail(1, 1, "missing kernel name"))?;
    if !is_binding(&name.text) || arity(&name.text).is_some() {
        return Err(fail(name.line, name.column, "invalid kernel name"));
    }
    if name.text != get("kernel") {
        return Err(fail(
            name.line,
            name.column,
            &format!(
                "kernel '{}' does not match @kernel '{}'",
                name.text,
                get("kernel")
            ),
        ));
    }
    parser.expect("[")?;
    let mut bindings: Vec<String> = Vec::new();
    loop {
        let token = parser.next().cloned().ok_or(fail(1, 1, "unclosed '['"))?;
        if token.text == "]" {
            break;
        }
        if [")", "(", "["].contains(&token.text.as_str()) {
            return Err(fail(token.line, token.column, "expected a binding name"));
        }
        if !is_binding(&token.text) {
            return Err(fail(
                token.line,
                token.column,
                "binding must start with a letter or underscore",
            ));
        }
        if bindings.iter().any(|b| b == &token.text) {
            return Err(fail(token.line, token.column, "duplicate binding"));
        }
        if bindings.len() == 32 {
            return Err(fail(token.line, token.column, "too many inputs"));
        }
        bindings.push(token.text);
    }
    if bindings.is_empty() {
        let t = parser.peek().cloned();
        let (line, column) = t.map(|t| (t.line, t.column)).unwrap_or((1, 1));
        return Err(fail(line, column, "kernel declares no inputs"));
    }
    parser.bindings = &[];

    // Body: either `(rgba r g b a)` (4 outputs) or scalar expressions.
    let body_parser = Parser {
        tokens: &tokens,
        pos: parser.pos,
        bindings: &bindings,
    };
    let mut body = body_parser;
    let outputs: u32;
    if body.peek().map(|t| t.text.as_str()) == Some("(")
        && tokens.get(body.pos + 1).map(|t| t.text.as_str()) == Some("rgba")
    {
        body.next(); // (
        body.next(); // rgba
        for _ in 0..4 {
            body.expression(0)?;
        }
        body.expect(")")?;
        outputs = 4;
    } else {
        outputs = 1;
        body.expression(0)?;
    }
    body.expect(")")?;
    if body.pos != tokens.len() {
        let t = tokens[body.pos].clone();
        return Err(fail(t.line, t.column, "trailing source after kernel"));
    }

    Ok(KernelInfo {
        name: name.text,
        category: get("category"),
        inputs: bindings,
        outputs,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const BRIGHTNESS: &str = ";; @kernel brightness\n;; @category color\n\
        ;; @description Brightness on premultiplied RGBA.\n;; @complexity Low\n\
        ;; @gpu Yes\n;; @since 0.2.0\n\
        (defkernel brightness [r g b a amount]\n  (rgba (* r amount) (* g amount) (* b amount) a))";

    #[test]
    fn valid_kernel_reports_facts() {
        let info = validate_source(BRIGHTNESS).expect("valid kernel");
        assert_eq!(info.name, "brightness");
        assert_eq!(info.category, "color");
        assert_eq!(info.inputs.len(), 5);
        assert_eq!(info.outputs, 4);
    }

    #[test]
    fn missing_metadata_fails() {
        let err = validate_source("(defkernel brightness [r] r)").unwrap_err();
        assert!(err.message.contains("@kernel"), "{err}");
    }

    #[test]
    fn unbalanced_paren_reports_position() {
        let src = BRIGHTNESS.replacen("(rgba", "(rgba (", 1);
        let err = validate_source(&src).unwrap_err();
        assert!(err.line >= 1, "{err}");
        assert!(err.message.contains("unclosed"), "{err}");
    }

    #[test]
    fn unknown_operation_fails() {
        let src = BRIGHTNESS.replace("(* r amount)", "(blur r amount)");
        let err = validate_source(&src).unwrap_err();
        assert!(err.message.contains("unknown operation"), "{err}");
    }

    #[test]
    fn duplicate_binding_fails() {
        let src = BRIGHTNESS.replace("[r g b a amount]", "[r r b a amount]");
        let err = validate_source(&src).unwrap_err();
        assert!(err.message.contains("duplicate"), "{err}");
    }

    #[test]
    fn unbound_symbol_fails() {
        let src = BRIGHTNESS.replace("(* r amount)", "(+ r missing)");
        let err = validate_source(&src).unwrap_err();
        assert!(err.message.contains("undefined binding"), "{err}");
    }

    #[test]
    fn name_must_match_metadata() {
        let src = BRIGHTNESS.replace("defkernel brightness", "defkernel other");
        let err = validate_source(&src).unwrap_err();
        assert!(err.message.contains("@kernel"), "{err}");
    }
}
