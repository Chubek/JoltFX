;; Ingagi AM rewrite #15: reassociate-add
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite reassociate-add
  (add (add ?x ?y) ?z)
  (add ?x (add ?y ?z)))
