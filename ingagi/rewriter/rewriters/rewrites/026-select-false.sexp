;; Ingagi AM rewrite #26: select-false
;; Category: control-flow
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite select-false
  (select (const false) ?a ?b)
  ?b)
