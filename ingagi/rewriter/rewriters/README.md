# Ingagi Abstract-Machine Rewrites

This archive contains 50 illustrative S-expression rewrite specifications, grouped by category in their filenames.

## Format

Each file uses the illustrative form:

```lisp
(rewrite rule-name
  left-hand-side-pattern
  right-hand-side-expression)
```

Adapt the wrapper and pattern syntax to your e-graph/TRS engine. This archive has not been validated against your engine's parser.

## Correctness caveats

These are starting templates, not a production-verified rule set. Add guards or remove rules where Ingagi AM semantics require it:

- Constant folding assumes a defined arithmetic evaluator and overflow policy.
- Reassociation can change floating-point rounding; restrict it to integer arithmetic or an appropriate fast-math mode.
- Self-comparison rewrites may be invalid for floating-point NaNs or unusual comparison semantics.
- `dead-pure-op` is schematic; it must only remove effect-free operations whose result is unused.
- `identity-cast` and `nested-cast` require type- and representation-aware validation.
- ECS known-present/known-absent rules require proven world-state facts.
- GPU load/store forwarding requires aliasing, ordering, and visibility proofs.
- A repeated GPU barrier can be removed only when the memory model proves it redundant.
- Duplicate `neg-neg` and `double-negation` are retained to mirror the requested initial set; remove duplicates if desired.
