;; Ingagi AM rewrite #23: lt-self
;; Category: comparison
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite lt-self
  (lt ?x ?x)
  (const false))
