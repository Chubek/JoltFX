;; Ingagi AM rewrite #36: dot-zero
;; Category: vector
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite dot-zero
  (vec.dot ?v (vec.zero))
  (const 0))
