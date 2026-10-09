;; Ingagi AM rewrite #4: mul-one
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite mul-one
  (mul ?x (const 1))
  ?x)
