;; Ingagi AM rewrite #9: div-one
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite div-one
  (div ?x (const 1))
  ?x)
