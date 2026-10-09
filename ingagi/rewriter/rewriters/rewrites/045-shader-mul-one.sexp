;; Ingagi AM rewrite #45: shader-mul-one
;; Category: shader
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite shader-mul-one
  (shader.mul ?x (shader.const 1))
  ?x)
