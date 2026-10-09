;; Ingagi AM rewrite #11: add-constants
;; Category: arithmetic
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite add-constants
  (add (const ?a) (const ?b))
  (const (+ ?a ?b)))
