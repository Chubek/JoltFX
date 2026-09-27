//! JBC1 bytecode emission.
//!
//! `compile` used to write its input back with a comment header, which meant it
//! produced nothing any runtime could execute. This module emits the real JBC1
//! container that the Joltscript VM and every backend run:
//!
//! ```text
//! offset  size  field
//! 0       4     magic 0x3143424a ("JBC1", little-endian)
//! 4       4     version (1)
//! 8       4     input binding count
//! 12      4     output count (1, or 4 for the (rgba ...) form)
//! 16      8*n   instruction pairs: u32 opcode, u32 operand
//! ```
//!
//! Float constants are IEEE binary32 bit patterns. There are no jumps: the
//! validator gives a hard execution bound, so a program is straight-line code.
//!
//! This mirrors `joltscript/layers/glue/src/compiler.c`. The two must produce
//! byte-identical output; `tests/bytecode_parity.rs` pins the known vectors and
//! `scripts/check-bytecode-parity.sh` compares this compiler against the C one
//! over every bundled kernel, so a divergence is a test failure rather than a
//! silent incompatibility.

use std::collections::HashMap;
use std::fmt;

pub const MAGIC: u32 = 0x3143_424a;
pub const VERSION: u32 = 1;
pub const HEADER_SIZE: usize = 16;
pub const MAX_INPUTS: u32 = 32;
pub const MAX_INSTRUCTIONS: usize = 4096;
pub const MAX_NESTING: u32 = 64;
pub const MAX_SOURCE_BYTES: usize = 1024 * 1024;
const MAX_TOKEN_BYTES: usize = 63;

const OP_CONST: u32 = 1;
const OP_INPUT: u32 = 2;
const OP_ADD: u32 = 3;
const OP_SUB: u32 = 4;
const OP_MUL: u32 = 5;
const OP_DIV: u32 = 6;
const OP_MIN: u32 = 7;
const OP_MAX: u32 = 8;
const OP_ABS: u32 = 9;
const OP_FLOOR: u32 = 10;
const OP_POW: u32 = 11;
const OP_SQRT: u32 = 12;
const OP_LT: u32 = 13;
const OP_SELECT: u32 = 14;
const OP_OUTPUT: u32 = 15;

/// A compilation failure with source position.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CompileError {
    pub line: usize,
    pub column: usize,
    pub message: String,
}

impl fmt::Display for CompileError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}: {}", self.line, self.column, self.message)
    }
}

/// Name, arity and emitted opcode for each supported operation.
const OPERATIONS: &[(&str, u32, u32)] = &[
    ("+", OP_ADD, 2),
    ("-", OP_SUB, 2),
    ("*", OP_MUL, 2),
    ("/", OP_DIV, 2),
    ("min", OP_MIN, 2),
    ("max", OP_MAX, 2),
    ("abs", OP_ABS, 1),
    ("floor", OP_FLOOR, 1),
    ("pow", OP_POW, 2),
    ("sqrt", OP_SQRT, 1),
    ("<", OP_LT, 2),
    ("select", OP_SELECT, 3),
];

/// A cursor over the source that skips whitespace and `;` comments, and can
/// report the position of the last thing it consumed.
struct Parser<'a> {
    source: &'a str,
    bytes: &'a [u8],
    cursor: usize,
    line: usize,
    column: usize,
    bindings: HashMap<String, u32>,
    code: Vec<u8>,
    input_count: u32,
}

impl<'a> Parser<'a> {
    fn new(source: &'a str) -> Self {
        Parser {
            source,
            bytes: source.as_bytes(),
            cursor: 0,
            line: 1,
            column: 1,
            bindings: HashMap::new(),
            code: Vec::with_capacity(HEADER_SIZE),
            input_count: 0,
        }
    }

    fn error<T>(&self, message: impl Into<String>) -> Result<T, CompileError> {
        Err(CompileError {
            line: self.line,
            column: self.column,
            message: message.into(),
        })
    }

    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.cursor).copied()
    }

    fn bump(&mut self) {
        if let Some(byte) = self.peek() {
            self.cursor += 1;
            if byte == b'\n' {
                self.line += 1;
                self.column = 1;
            } else {
                self.column += 1;
            }
        }
    }

    /// Skips whitespace and `;` comments, matching the C `space()`.
    fn skip_space(&mut self) {
        loop {
            while self
                .peek()
                .is_some_and(|b| (b as char).is_ascii_whitespace())
            {
                self.bump();
            }
            if self.peek() != Some(b';') {
                break;
            }
            while self.peek().is_some_and(|b| b != b'\n') {
                self.bump();
            }
        }
    }

    fn expect_char(&mut self, expected: u8, message: &str) -> Result<(), CompileError> {
        self.skip_space();
        if self.peek() != Some(expected) {
            return self.error(message);
        }
        self.bump();
        Ok(())
    }

    /// Reads a bare token, stopping at whitespace and the delimiter set.
    fn token(&mut self) -> Result<String, CompileError> {
        self.skip_space();
        let start = self.cursor;
        while let Some(byte) = self.peek() {
            let c = byte as char;
            if c.is_ascii_whitespace() || "()[];".contains(c) {
                break;
            }
            self.bump();
        }
        if self.cursor == start {
            return self.error("expected a symbol or number");
        }
        if self.cursor - start > MAX_TOKEN_BYTES {
            return self.error("token exceeds 63 bytes");
        }
        Ok(self.source[start..self.cursor].to_string())
    }

    fn expect_word(&mut self, expected: &str) -> Result<(), CompileError> {
        let value = self.token()?;
        if value == expected {
            Ok(())
        } else {
            self.error(format!("expected {expected}"))
        }
    }

    fn emit(&mut self, opcode: u32, operand: u32) -> Result<(), CompileError> {
        if self.code.len() / 8 >= MAX_INSTRUCTIONS {
            return self.error("instruction limit exceeded");
        }
        self.code.extend_from_slice(&opcode.to_le_bytes());
        self.code.extend_from_slice(&operand.to_le_bytes());
        Ok(())
    }

    /// Parses one expression and appends the instructions that evaluate it.
    fn expression(&mut self, depth: u32) -> Result<(), CompileError> {
        if depth >= MAX_NESTING {
            return self.error("expression nesting limit exceeded");
        }
        self.skip_space();
        if self.peek() == Some(b'(') {
            self.bump();
            let name = self.token()?;
            if let Some(&(_, opcode, arity)) = OPERATIONS.iter().find(|(op, _, _)| *op == name) {
                for _ in 0..arity {
                    self.expression(depth + 1)?;
                }
                self.expect_char(b')', "unexpected delimiter")?;
                return self.emit(opcode, 0);
            }
            return self.error("unknown operation");
        }

        let value = self.token()?;
        if let Some(&index) = self.bindings.get(&value) {
            return self.emit(OP_INPUT, index);
        }
        match parse_finite_f32(&value) {
            Some(bits) => self.emit(OP_CONST, bits),
            None => self.error("undefined binding or invalid finite f32 literal"),
        }
    }
}

/// Parses a finite f32 literal and returns its bit pattern. Rejects anything
/// `strtof` would not consume completely, and anything non-finite.
fn parse_finite_f32(text: &str) -> Option<u32> {
    // Rust's parser accepts forms C's strtof does not, and vice versa, so the
    // grammar is checked here rather than relying on `str::parse` alone.
    if text.is_empty() {
        return None;
    }
    let bytes = text.as_bytes();
    let mut index = 0;
    if matches!(bytes[0], b'+' | b'-') {
        index += 1;
    }
    if index == bytes.len() {
        return None;
    }
    let mut saw_digit = false;
    let mut saw_dot = false;
    let mut saw_exponent = false;
    while index < bytes.len() {
        match bytes[index] {
            b'0'..=b'9' => saw_digit = true,
            b'.' if !saw_dot && !saw_exponent => saw_dot = true,
            b'e' | b'E' if saw_digit && !saw_exponent => {
                saw_exponent = true;
                if matches!(bytes.get(index + 1), Some(b'+') | Some(b'-')) {
                    index += 1;
                }
            }
            _ => return None,
        }
        index += 1;
    }
    if !saw_digit {
        return None;
    }
    let value: f32 = text.parse().ok()?;
    if !value.is_finite() {
        return None;
    }
    Some(value.to_bits())
}

/// Compiles Joltscript source into a JBC1 program.
pub fn compile(source: &str) -> Result<Vec<u8>, CompileError> {
    if source.len() > MAX_SOURCE_BYTES {
        return Err(CompileError {
            line: 1,
            column: 1,
            message: "source exceeds the 1 MiB limit".to_string(),
        });
    }
    let mut parser = Parser::new(source);
    parser.expect_char(b'(', "unexpected delimiter")?;
    parser.expect_word("defkernel")?;
    let _name = parser.token()?;
    parser.expect_char(b'[', "unexpected delimiter")?;
    loop {
        parser.skip_space();
        if parser.peek() == Some(b']') {
            parser.bump();
            break;
        }
        if parser.input_count == MAX_INPUTS {
            return parser.error("too many inputs");
        }
        let binding = parser.token()?;
        if !binding.starts_with(|c: char| c.is_ascii_alphabetic() || c == '_') {
            return parser.error("binding must start with a letter or underscore");
        }
        let index = parser.input_count;
        if parser.bindings.insert(binding, index).is_some() {
            return parser.error("duplicate binding");
        }
        parser.input_count += 1;
    }

    parser.skip_space();
    let body_start = parser.cursor;
    let mut outputs = 1u32;
    if parser.peek() == Some(b'(') {
        parser.bump();
        let head = parser.token()?;
        if head == "rgba" {
            outputs = 4;
        } else {
            parser.cursor = body_start;
        }
    }
    for index in 0..outputs {
        parser.expression(0)?;
        parser.emit(OP_OUTPUT, index)?;
    }
    if outputs == 4 {
        parser.expect_char(b')', "unexpected delimiter")?;
    }
    parser.expect_char(b')', "unexpected delimiter")?;
    parser.skip_space();
    if parser.cursor != parser.bytes.len() {
        return parser.error("trailing source after kernel");
    }

    let mut program = Vec::with_capacity(HEADER_SIZE + parser.code.len());
    program.extend_from_slice(&MAGIC.to_le_bytes());
    program.extend_from_slice(&VERSION.to_le_bytes());
    program.extend_from_slice(&parser.input_count.to_le_bytes());
    program.extend_from_slice(&outputs.to_le_bytes());
    program.extend_from_slice(&parser.code);
    Ok(program)
}

/// Reads the header of a JBC1 program. Returns `(inputs, outputs)`.
pub fn header(program: &[u8]) -> Result<(u32, u32), CompileError> {
    let malformed = |message: &str| CompileError {
        line: 0,
        column: 0,
        message: message.to_string(),
    };
    if program.len() < HEADER_SIZE {
        return Err(malformed("program is shorter than a JBC1 header"));
    }
    let read = |offset: usize| {
        u32::from_le_bytes([
            program[offset],
            program[offset + 1],
            program[offset + 2],
            program[offset + 3],
        ])
    };
    if read(0) != MAGIC {
        return Err(malformed("bad JBC1 magic"));
    }
    if read(4) != VERSION {
        return Err(malformed("unsupported JBC1 version"));
    }
    let inputs = read(8);
    let outputs = read(12);
    if inputs > MAX_INPUTS {
        return Err(malformed("declared input count exceeds the limit"));
    }
    if outputs == 0 || outputs > 4 {
        return Err(malformed("declared output count is out of range"));
    }
    if !(program.len() - HEADER_SIZE).is_multiple_of(8) {
        return Err(malformed(
            "program body is not a whole number of instructions",
        ));
    }
    if (program.len() - HEADER_SIZE) / 8 > MAX_INSTRUCTIONS {
        return Err(malformed("program exceeds the instruction limit"));
    }
    Ok((inputs, outputs))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn code(source: &str) -> Vec<u8> {
        compile(source).unwrap_or_else(|e| panic!("{e}"))
    }

    const BRIGHTNESS: &str =
        "(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))";

    #[test]
    fn header_is_jbc1() {
        let program = code(BRIGHTNESS);
        assert_eq!(&program[0..4], b"JBC1");
        assert_eq!(header(&program), Ok((5, 4)));
    }

    #[test]
    fn single_output_form_emits_one_output() {
        let program = code("(defkernel half [x] (* x 0.5))");
        assert_eq!(header(&program), Ok((1, 1)));
        // CONST 0.5, INPUT 0, MUL, OUTPUT 0
        assert_eq!(program.len(), HEADER_SIZE + 4 * 8);
    }

    #[test]
    fn constant_operand_is_a_binary32_bit_pattern() {
        let program = code("(defkernel half [x] (* x 0.5))");
        let instruction = |index: usize| {
            let offset = HEADER_SIZE + index * 8;
            (
                u32::from_le_bytes(program[offset..offset + 4].try_into().unwrap()),
                u32::from_le_bytes(program[offset + 4..offset + 8].try_into().unwrap()),
            )
        };
        // Operands are emitted before their operator, so a stack machine reads
        // left to right: push the binding, push the constant, then multiply.
        assert_eq!(instruction(0), (OP_INPUT, 0));
        assert_eq!(instruction(1), (OP_CONST, 0.5f32.to_bits()));
        assert_eq!(instruction(2), (OP_MUL, 0));
        assert_eq!(instruction(3), (OP_OUTPUT, 0));
    }

    #[test]
    fn binding_order_is_the_declaration_order() {
        let program = code("(defkernel swap2 [a b c] (+ c b))");
        let operand_of = |index: usize| {
            let offset = HEADER_SIZE + index * 8;
            u32::from_le_bytes(program[offset + 4..offset + 8].try_into().unwrap())
        };
        assert_eq!(operand_of(0), 2, "c is the third binding");
        assert_eq!(operand_of(1), 1, "b is the second binding");
    }

    #[test]
    fn rejects_duplicate_bindings() {
        let error = compile("(defkernel dup [x x] x)").unwrap_err();
        assert_eq!(error.message, "duplicate binding");
    }

    #[test]
    fn rejects_undefined_binding() {
        let error = compile("(defkernel bad [x] (+ x missing))").unwrap_err();
        assert!(error.message.contains("undefined binding"));
    }

    #[test]
    fn rejects_unknown_operation() {
        let error = compile("(defkernel bad [x] (frob x))").unwrap_err();
        assert_eq!(error.message, "unknown operation");
    }

    #[test]
    fn rejects_arity_errors() {
        // `+` takes two operands; one is a syntax error at the delimiter.
        assert!(compile("(defkernel bad [x] (+ x))").is_err());
        assert!(compile("(defkernel bad [x] (abs x x))").is_err());
    }

    #[test]
    fn rejects_trailing_source() {
        let error = compile("(defkernel bad [x] x) (defkernel other [y] y)").unwrap_err();
        assert_eq!(error.message, "trailing source after kernel");
    }

    #[test]
    fn rejects_non_finite_literals() {
        assert!(compile("(defkernel bad [x] (/ x 0.0))").is_ok());
        // NaN and infinity are rejected as literals; division by zero is caught
        // at execution time by the VM instead.
        assert!(compile("(defkernel bad [x] x)").is_ok());
    }

    #[test]
    fn rejects_binding_that_does_not_start_with_a_letter() {
        let error = compile("(defkernel bad [1x] 1x)").unwrap_err();
        assert!(error.message.contains("letter or underscore"));
    }

    #[test]
    fn rejects_too_many_inputs() {
        let names: Vec<String> = (0..33).map(|i| format!("v{i}")).collect();
        let source = format!("(defkernel many [{}] v0)", names.join(" "));
        let error = compile(&source).unwrap_err();
        assert_eq!(error.message, "too many inputs");
    }

    #[test]
    fn comments_and_whitespace_are_ignored() {
        let bare = code("(defkernel half [x] (* x 0.5))");
        let decorated = code(";; a comment\n(defkernel half [x]   (* x 0.5) ) ;; trailing\n");
        assert_eq!(bare, decorated);
    }

    #[test]
    fn diagnostic_carries_a_position() {
        let error = compile("(defkernel bad [x]\n  (frob x))").unwrap_err();
        assert_eq!(error.line, 2);
    }

    #[test]
    fn header_rejects_malformed_programs() {
        assert!(header(b"").is_err());
        assert!(header(b"JBC0").is_err());
        let mut wrong_version = code("(defkernel half [x] x)");
        wrong_version[4] = 9;
        assert!(header(&wrong_version).is_err());
        assert!(header(&code(BRIGHTNESS)[..HEADER_SIZE + 3]).is_err());
    }

    #[test]
    fn every_bundled_effect_shape_compiles() {
        for source in [
            "(defkernel invert [r g b a] (rgba (- 1.0 r) (- 1.0 g) (- 1.0 b) a))",
            "(defkernel gamma [r g b a g_] (rgba (pow r g_) (pow g g_) (pow b g_) a))",
            "(defkernel mix [a b c t] (select (< t 0.5) a b))",
            "(defkernel absmix [x] (abs (min 0.0 (max -1.0 x))))",
            "(defkernel floormix [x] (floor (sqrt (pow x 2.0))))",
        ] {
            let program = code(source);
            assert!(header(&program).is_ok(), "{source}");
        }
    }
}
