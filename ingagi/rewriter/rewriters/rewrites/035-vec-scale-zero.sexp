;; Ingagi AM rewrite #35: vec-scale-zero
;; Category: vector
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite vec-scale-zero
  (vec.scale ?v (const 0))
  (vec.zero))
