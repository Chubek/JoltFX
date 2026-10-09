;; Ingagi AM rewrite #14: double-negation
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite double-negation
  (neg (neg ?x))
  ?x)
