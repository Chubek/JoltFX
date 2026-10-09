;; Ingagi AM rewrite #28: branch-false
;; Category: control-flow
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite branch-false
  (branch (const false) ?then ?else)
  (jump ?else))
