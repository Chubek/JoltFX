;; Ingagi AM rewrite #32: dead-pure-op
;; Category: dead-code
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite dead-pure-op
  (let ?v (pure-op ?args...) ?body)
  (let ?body))
