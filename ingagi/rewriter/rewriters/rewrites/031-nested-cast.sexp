;; Ingagi AM rewrite #31: nested-cast
;; Category: ssa
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite nested-cast
  (cast ?t2 (cast ?t1 ?x))
  (cast ?t2 ?x))
