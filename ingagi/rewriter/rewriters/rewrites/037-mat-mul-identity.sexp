;; Ingagi AM rewrite #37: mat-mul-identity
;; Category: matrix
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite mat-mul-identity
  (mat.mul ?m (mat.identity))
  ?m)
