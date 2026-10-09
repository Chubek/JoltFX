;; Ingagi AM rewrite #29: phi-same-value
;; Category: ssa
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite phi-same-value
  (phi ?x ?x)
  ?x)
