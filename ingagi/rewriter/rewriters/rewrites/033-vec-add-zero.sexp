;; Ingagi AM rewrite #33: vec-add-zero
;; Category: vector
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite vec-add-zero
  (vec.add ?v (vec.zero))
  ?v)
