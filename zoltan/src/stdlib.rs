//! Bundled creative-programming stdlib snippets.
//!
//! Every snippet is plain JBC1 scalar-expression source that can be pasted
//! into a sketch body. They only use operations both compilers implement
//! (`+ - * / min max abs floor pow sqrt < > <= >= = != and or not
//! bitwise-and bitwise-or bitwise-xor shl shr select`), so stdlib code never
//! breaks the byte-parity gate.

pub struct Snippet {
    pub name: &'static str,
    pub description: &'static str,
    pub source: &'static str,
}

pub const SNIPPETS: &[Snippet] = &[
    Snippet {
        name: "sketch",
        description: "Minimal animated sketch: x/y gradient drifting with time.",
        source: ";; @kernel sketch_demo\n;; @category generative\n\
;; @description Minimal creative sketch (x/y gradient + time).\n;; @complexity Low\n\
;; @gpu Yes\n;; @since 0.3.0\n;; @canvas 320x180\n\
(defkernel sketch_demo [x y t]\n  (rgba x y (* 0.5 (+ 0.5 (* 0.5 t))) 1.0))",
    },
    Snippet {
        name: "circle",
        description: "Centered disc via clamped distance (SDF-style, no sqrt needed).",
        source: "(select (< (+ (* (- x 0.5) (- x 0.5)) (* (- y 0.5) (- y 0.5))) 0.04)\n  1.0 0.0)",
    },
    Snippet {
        name: "palette",
        description: "Cheap cosine-free palette: three phase-shifted channels from t.",
        source: "(rgba (* 0.5 (+ 1.0 (* x t))) (* 0.5 (+ 1.0 (* y t))) (* 0.5 (+ 1.0 (* 0.5 (+ x y)))) 1.0)",
    },
    Snippet {
        name: "stripes",
        description: "Animated vertical stripes using floor + select.",
        source: "(select (= (floor (+ (* x 8.0) t)) (* 2.0 (floor (* 0.5 (+ (* x 8.0) t))))) 1.0 0.2)",
    },
    Snippet {
        name: "mouse_mix",
        description: "Mix two gradients with the mx pointer binding (declare mx in inputs).",
        source: "(rgba (select (< x mx) x y) (select (< y my) y x) t 1.0)",
    },
    Snippet {
        name: "soft_edge",
        description: "Smooth-ish edge with min/max clamp instead of smoothstep.",
        source: "(max 0.0 (min 1.0 (* (- 0.6 (+ (* (- x 0.5) (- x 0.5)) (* (- y 0.5) (- y 0.5)))) 8.0)))",
    },
];

pub fn list() -> &'static [Snippet] {
    SNIPPETS
}

pub fn show(name: &str) -> Option<&'static Snippet> {
    SNIPPETS.iter().find(|s| s.name == name)
}
