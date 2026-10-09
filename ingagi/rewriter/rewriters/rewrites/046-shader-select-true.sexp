;; Ingagi AM rewrite #46: shader-select-true
;; Category: shader
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite shader-select-true
  (shader.select (shader.const true) ?a ?b)
  ?a)
