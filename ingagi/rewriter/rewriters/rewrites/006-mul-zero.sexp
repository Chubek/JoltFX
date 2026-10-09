;; Ingagi AM rewrite #6: mul-zero
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite mul-zero
  (mul ?x (const 0))
  (const 0))
