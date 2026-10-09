;; Ingagi AM rewrite #19: or-true
;; Category: boolean
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite or-true
  (or ?x (const true))
  (const true))
