;; Ingagi AM rewrite #5: mul-one-left
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite mul-one-left
  (mul (const 1) ?x)
  ?x)
