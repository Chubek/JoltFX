;; Ingagi AM rewrite #7: mul-zero-left
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite mul-zero-left
  (mul (const 0) ?x)
  (const 0))
