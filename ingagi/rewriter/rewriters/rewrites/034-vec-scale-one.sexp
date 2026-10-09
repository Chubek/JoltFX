;; Ingagi AM rewrite #34: vec-scale-one
;; Category: vector
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite vec-scale-one
  (vec.scale ?v (const 1))
  ?v)
