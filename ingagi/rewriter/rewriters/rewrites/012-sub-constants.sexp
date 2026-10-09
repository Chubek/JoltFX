;; Ingagi AM rewrite #12: sub-constants
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite sub-constants
  (sub (const ?a) (const ?b))
  (const (- ?a ?b)))
