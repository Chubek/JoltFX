;; Ingagi AM rewrite #20: not-not
;; Category: boolean
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite not-not
  (not (not ?x))
  ?x)
