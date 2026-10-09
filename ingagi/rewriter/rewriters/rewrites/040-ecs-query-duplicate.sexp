;; Ingagi AM rewrite #40: ecs-query-duplicate
;; Category: ecs
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite ecs-query-duplicate
  (ecs.query (with ?c ?c) ?filters)
  (ecs.query (with ?c) ?filters))
