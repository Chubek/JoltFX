//! Independent version-2 tree emitter; safe bridge to the shared typed checker/VM.
use crate::bytecode::CompileError;
use std::alloc::{alloc, dealloc, Layout};
use std::ffi::{c_char, c_int, c_void, CStr};

#[no_mangle]
extern "C" fn jt_host_alloc(size: usize) -> *mut c_void {
    let Some(total) = size.checked_add(16) else {
        return std::ptr::null_mut();
    };
    let Ok(layout) = Layout::from_size_align(total, 16) else {
        return std::ptr::null_mut();
    };
    // SAFETY: the allocation header stores the exact layout for jt_host_free.
    unsafe {
        let p = alloc(layout);
        if p.is_null() {
            return p.cast();
        }
        p.cast::<usize>().write(total);
        p.add(16).cast()
    }
}
#[no_mangle]
extern "C" fn jt_host_free(pointer: *mut c_void) {
    if pointer.is_null() {
        return;
    }
    // SAFETY: only pointers returned by jt_host_alloc cross this private bridge.
    unsafe {
        let p = pointer.cast::<u8>().sub(16);
        let size = p.cast::<usize>().read();
        dealloc(p, Layout::from_size_align(size, 16).unwrap());
    }
}
#[repr(C)]
struct Error {
    line: usize,
    column: usize,
    message: [c_char; 160],
}
extern "C" {
    fn jt_prepare(code: *mut u8, size: usize, error: *mut Error) -> c_int;
    fn jt_check(code: *const u8, size: usize, error: *mut Error) -> c_int;
    fn jt_run_float(
        code: *const u8,
        size: usize,
        inputs: *const f32,
        ni: usize,
        outputs: *mut f32,
        no: usize,
    ) -> c_int;
}
fn diagnostic(status: i32, error: Error) -> CompileError {
    // SAFETY: C writes a bounded, NUL-terminated diagnostic into the zeroed array.
    let message = unsafe { CStr::from_ptr(error.message.as_ptr()) }
        .to_string_lossy()
        .into_owned();
    CompileError {
        line: error.line.max(1),
        column: error.column.max(1),
        message: if message.is_empty() {
            format!("invalid typed bytecode ({status})")
        } else {
            message
        },
    }
}
pub fn validate(code: &[u8]) -> Result<(), CompileError> {
    let mut error = Error {
        line: 0,
        column: 0,
        message: [0; 160],
    };
    // SAFETY: slices and diagnostic remain alive; C checks all offsets and lengths.
    let status = unsafe { jt_check(code.as_ptr(), code.len(), &mut error) };
    if status == 0 {
        Ok(())
    } else {
        Err(diagnostic(status, error))
    }
}
pub fn evaluate(code: &[u8], inputs: &[f32], outputs: usize) -> Result<Vec<f32>, String> {
    if outputs > 4 {
        return Err("typed output count exceeds four".into());
    }
    let mut result = vec![0.; outputs];
    // SAFETY: the VM validates bytecode and buffer lengths before execution.
    let status = unsafe {
        jt_run_float(
            code.as_ptr(),
            code.len(),
            inputs.as_ptr(),
            inputs.len(),
            result.as_mut_ptr(),
            outputs,
        )
    };
    if status == 0 {
        Ok(result)
    } else {
        Err(format!("typed execution failed ({status})"))
    }
}
pub fn required(source: &str) -> bool {
    let clean = source
        .lines()
        .map(|line| line.split(';').next().unwrap_or(""))
        .collect::<Vec<_>>()
        .join("\n");
    if clean.contains(['"', '\\']) {
        return true;
    }
    clean
        .split(|c: char| c.is_ascii_whitespace() || "()[]".contains(c))
        .any(|word| {
            [
                "import",
                "module",
                "intrinsic",
                "export",
                "value",
                "function",
                "list",
                "vector",
                "set",
                "for",
                "while",
                "loop",
                "break",
                "continue",
                "fn",
                "defn",
                "->",
                "true",
                "false",
                "void",
                "i32",
                "i64",
                "u8",
                "u16",
                "u32",
                "u64",
                "f32",
                "f64",
                "bool",
                "char",
                "string",
            ]
            .contains(&word)
                || (word.starts_with(|c: char| c.is_ascii_digit() || c == '-' || c == '+')
                    && ["i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"]
                        .iter()
                        .any(|s| word.len() > s.len() && word.ends_with(s)))
        })
}
#[derive(Clone, Default)]
struct Node {
    words: [u32; 7],
}
struct Reader<'a> {
    source: &'a [u8],
    pos: usize,
    line: u32,
    column: u32,
    nodes: Vec<Node>,
    pool: Vec<u8>,
}
impl Reader<'_> {
    fn error<T>(&self, message: &str) -> Result<T, CompileError> {
        Err(CompileError {
            line: self.line as usize,
            column: self.column as usize,
            message: message.into(),
        })
    }
    fn peek(&self) -> u8 {
        self.source.get(self.pos).copied().unwrap_or(0)
    }
    fn advance(&mut self) {
        if self.peek() == b'\n' {
            self.line += 1;
            self.column = 1;
        } else {
            self.column += 1;
        }
        self.pos += 1;
    }
    fn space(&mut self) {
        loop {
            while self.peek().is_ascii_whitespace() {
                self.advance();
            }
            if self.peek() != b';' {
                break;
            }
            while self.peek() != 0 && self.peek() != b'\n' {
                self.advance();
            }
        }
    }
    fn form(&mut self, depth: usize) -> Result<usize, CompileError> {
        self.space();
        if depth >= 128 || self.nodes.len() == 4096 {
            return self.error("typed syntax budget exceeded");
        }
        let id = self.nodes.len();
        self.nodes.push(Node {
            words: [0, 0, 0, 0, 0, self.line, self.column],
        });
        let ch = self.peek();
        if ch == b'(' || ch == b'[' {
            self.nodes[id].words[0] = if ch == b'(' { 1 } else { 2 };
            let end = if ch == b'(' { b')' } else { b']' };
            self.advance();
            let mut last = 0;
            loop {
                self.space();
                if self.peek() == end {
                    self.advance();
                    return Ok(id);
                }
                if [0, b')', b']'].contains(&self.peek()) {
                    return self.error("unclosed or mismatched delimiter");
                }
                let child = self.form(depth + 1)?;
                if last == 0 {
                    self.nodes[id].words[1] = child as u32;
                } else {
                    self.nodes[last].words[2] = child as u32;
                }
                last = child;
            }
        }
        let start = self.pos;
        if ch == b'"' {
            self.nodes[id].words[0] = 3;
            self.advance();
            while self.peek() != 0 && self.peek() != b'"' {
                if self.peek() == b'\\' {
                    self.advance();
                    if self.peek() == 0 {
                        break;
                    }
                }
                self.advance();
            }
            if self.peek() != b'"' {
                return self.error("unterminated string");
            }
            self.advance();
        } else {
            while self.peek() != 0
                && !self.peek().is_ascii_whitespace()
                && !b"()[];".contains(&self.peek())
            {
                self.advance();
            }
        }
        if self.pos == start {
            return self.error("expected expression");
        }
        self.nodes[id].words[3] = self.pool.len() as u32;
        self.nodes[id].words[4] = (self.pos - start) as u32;
        self.pool.extend_from_slice(&self.source[start..self.pos]);
        Ok(id)
    }
}
pub fn compile(source: &str) -> Result<Vec<u8>, CompileError> {
    let mut r = Reader {
        source: source.as_bytes(),
        pos: 0,
        line: 1,
        column: 1,
        nodes: vec![Node::default()],
        pool: Vec::new(),
    };
    if source.len() > 1024 * 1024 || source.contains('\0') {
        return r.error("source exceeds budget or contains NUL");
    }
    let mut root = 0;
    let mut last = 0;
    loop {
        r.space();
        if r.peek() == 0 {
            break;
        }
        let n = r.form(0)?;
        if last == 0 {
            root = n;
        } else {
            r.nodes[last].words[2] = n as u32;
        }
        last = n;
    }
    if root == 0 {
        return r.error("expected defkernel");
    }
    let mut code = Vec::new();
    for word in [
        crate::bytecode::MAGIC,
        2,
        0,
        0,
        r.nodes.len() as u32,
        r.pool.len() as u32,
        root as u32,
        0,
    ] {
        code.extend_from_slice(&word.to_le_bytes());
    }
    for node in r.nodes {
        for word in node.words {
            code.extend_from_slice(&word.to_le_bytes());
        }
    }
    code.extend(r.pool);
    let mut error = Error {
        line: 0,
        column: 0,
        message: [0; 160],
    };
    // SAFETY: prepare validates the tree, writes only the two signature words.
    let status = unsafe { jt_prepare(code.as_mut_ptr(), code.len(), &mut error) };
    if status == 0 {
        Ok(code)
    } else {
        Err(diagnostic(status, error))
    }
}

/// Facts for the metadata validator, extracted after full bytecode validation.
pub fn facts(code: &[u8]) -> (String, Vec<String>) {
    let word = |p: usize| u32::from_le_bytes(code[p..p + 4].try_into().unwrap()) as usize;
    let pool = 32 + word(16) * 28;
    let node_word = |n: usize, w: usize| word(32 + n * 28 + w * 4);
    let text = |n: usize| {
        String::from_utf8_lossy(
            &code[pool + node_word(n, 3)..pool + node_word(n, 3) + node_word(n, 4)],
        )
        .into_owned()
    };
    let mut n = word(24);
    while n != 0 {
        let head = node_word(n, 1);
        if text(head) == "defkernel" {
            let name = node_word(head, 2);
            let params = node_word(name, 2);
            let mut p = node_word(params, 1);
            let mut inputs = Vec::new();
            while p != 0 {
                inputs.push(text(if node_word(p, 0) == 1 {
                    node_word(p, 1)
                } else {
                    p
                }));
                p = node_word(p, 2);
            }
            return (text(name), inputs);
        }
        n = node_word(n, 2);
    }
    let root = word(24);
    let head = node_word(root, 1);
    if text(head) == "module" {
        return (text(node_word(head, 2)), vec![]);
    }
    unreachable!("validated program has a kernel or module")
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn standard_library_modules_and_preview() {
        for source in [
            include_str!("../../joltscript/stdlib/math.jolt"),
            include_str!("../../joltscript/stdlib/geometry.jolt"),
            include_str!("../../joltscript/stdlib/graphics.jolt"),
            include_str!("../../joltscript/stdlib/time.jolt"),
            include_str!("../../joltscript/stdlib/collections.jolt"),
            include_str!("../../joltscript/stdlib/strings.jolt"),
        ] {
            let code = compile(source).unwrap();
            assert_eq!(crate::bytecode::header(&code).unwrap(), (0, 0));
            assert!(facts(&code).0.starts_with("jolt."));
            assert_eq!(
                crate::validate::validate_source(source).unwrap().category,
                "stdlib"
            );
            assert!(evaluate(&code, &[], 0).is_err());
        }
        let source = include_str!("../../joltscript/tests/conformance/phase2.jolt");
        let code = compile(source).unwrap();
        assert_eq!(
            evaluate(&code, &[0., 0., 0.], 4).unwrap(),
            [1., 11., 6., 1.25]
        );
        let aggregate =
            compile("(import jolt.collections) (defkernel k [] [\"x\" [1 2]])").unwrap();
        assert!(evaluate(&aggregate, &[], 1).is_err());
    }
    #[test]
    fn loops_functions_and_lexical_capture() {
        for (source, expected) in [
            ("(defkernel k [] (var n 0) (for [i 0 10] (set! n (+ n i))) n)",45.),
            ("(defkernel k [] (var n 0) (loop (set! n (+ n 1)) (when (= n 7) (break))) n)",7.),
            ("(defn fact [(n i32)] -> i32 (if (<= n 1) 1 (* n (fact (- n 1))))) (defkernel k [] (fact 6))",720.),
            ("(defkernel k [] (let a 7) (let f (fn [(x i32)] (+ a x))) (let [a 99] (f 3)))",10.),
            ("(defkernel k [] (var n 0) (let f (fn [] (set! n (+ n 1)))) (f) (f) n)",2.),
            ("(defn first [] (second)) (defn second [] 7) (defkernel k [] (first))",7.),
        ] {
            let code=crate::bytecode::compile(source).unwrap();
            assert_eq!(crate::bytecode::header(&code).unwrap(),(0,1));
            assert_eq!(evaluate(&code,&[],1).unwrap(),[expected]);
            assert_eq!(facts(&code),("k".into(),vec![]));
        }
    }
    #[test]
    fn typed_primitives_and_malformed_programs() {
        let source = "(defkernel k [] (rgba 18446744073709551615u64 \"héllo\\u{1F600}\" \\a true))";
        let code = compile(source).unwrap();
        assert_eq!(crate::bytecode::header(&code).unwrap(), (0, 4));
        assert!(evaluate(&code, &[], 4).is_err()); // float preview cannot consume string lanes
        for end in 0..code.len() {
            assert!(validate(&code[..end]).is_err());
        }
        for source in [
            "(defkernel k [] 256u8)",
            "(defkernel k [] (break))",
            "(defkernel k [] (if true 1 \"x\"))",
            "(defkernel k [] ((fn [(x i32)] x) \"x\"))",
            "(defkernel k [] \\u{D800})",
            "(defkernel k [] \"\\q\")",
        ] {
            let error = compile(source).unwrap_err();
            assert!(error.line > 0 && error.column > 0 && !error.message.is_empty());
        }
        let code = compile("(defkernel k [] (loop ()))").unwrap();
        assert!(evaluate(&code, &[], 1).unwrap_err().contains("-9"));
    }
    #[test]
    fn metadata_and_typed_preview() {
        let source = include_str!("../../joltscript/tests/conformance/phase1.jolt");
        let info = crate::validate::validate_source(source).unwrap();
        assert_eq!(info.name, "phase1");
        assert_eq!(info.inputs, ["x", "y", "t"]);
        let code = crate::bytecode::compile(source).unwrap();
        assert_eq!(
            evaluate(&code, &[0., 0., 0.], 4).unwrap(),
            [131., 0., 0., 1.]
        );
    }
}
