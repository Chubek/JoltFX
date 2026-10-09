;; Ingagi AM rewrite #25: select-true
;; Category: control-flow
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite select-true
  (select (const true) ?a ?b)
  ?a)
