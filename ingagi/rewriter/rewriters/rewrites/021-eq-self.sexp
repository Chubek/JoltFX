;; Ingagi AM rewrite #21: eq-self
;; Category: comparison
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite eq-self
  (eq ?x ?x)
  (const true))
