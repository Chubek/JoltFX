;; Ingagi AM rewrite #18: or-false
;; Category: boolean
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite or-false
  (or ?x (const false))
  ?x)
