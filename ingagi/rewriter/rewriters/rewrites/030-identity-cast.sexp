;; Ingagi AM rewrite #30: identity-cast
;; Category: ssa
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite identity-cast
  (cast ?type ?x)
  ?x)
