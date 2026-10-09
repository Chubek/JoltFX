;; Ingagi AM rewrite #22: ne-self
;; Category: comparison
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite ne-self
  (ne ?x ?x)
  (const false))
