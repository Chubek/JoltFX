;; Ingagi AM rewrite #16: and-true
;; Category: boolean
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite and-true
  (and ?x (const true))
  ?x)
