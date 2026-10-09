;; Ingagi AM rewrite #44: shader-add-zero
;; Category: shader
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite shader-add-zero
  (shader.add ?x (shader.const 0))
  ?x)
