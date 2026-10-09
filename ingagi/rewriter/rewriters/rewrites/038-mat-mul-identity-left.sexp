;; Ingagi AM rewrite #38: mat-mul-identity-left
;; Category: matrix
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite mat-mul-identity-left
  (mat.mul (mat.identity) ?m)
  ?m)
