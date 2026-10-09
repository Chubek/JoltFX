;; Ingagi AM rewrite #2: add-zero-right
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite add-zero-right
  (add (const 0) ?x)
  ?x)
