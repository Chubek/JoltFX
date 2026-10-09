;; Ingagi AM rewrite #47: shader-swizzle-identity
;; Category: shader
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite shader-swizzle-identity
  (shader.swizzle ?v xyzw)
  ?v)
