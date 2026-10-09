;; Ingagi AM rewrite #27: branch-true
;; Category: control-flow
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite branch-true
  (branch (const true) ?then ?else)
  (jump ?then))
