;; Ingagi AM rewrite #1: add-zero
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite add-zero
  (add ?x (const 0))
  ?x)
