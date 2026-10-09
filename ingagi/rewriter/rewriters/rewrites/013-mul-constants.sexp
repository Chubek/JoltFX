;; Ingagi AM rewrite #13: mul-constants
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite mul-constants
  (mul (const ?a) (const ?b))
  (const (* ?a ?b)))
