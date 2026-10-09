;; Ingagi AM rewrite #17: and-false
;; Category: boolean
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite and-false
  (and ?x (const false))
  (const false))
