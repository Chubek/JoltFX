;; Ingagi AM rewrite #10: neg-neg
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite neg-neg
  (neg (neg ?x))
  ?x)
