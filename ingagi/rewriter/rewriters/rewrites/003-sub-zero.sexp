;; Ingagi AM rewrite #3: sub-zero
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite sub-zero
  (sub ?x (const 0))
  ?x)
