;; Ingagi AM rewrite #43: ecs-entity-equal
;; Category: ecs
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite ecs-entity-equal
  (ecs.entity_eq ?e ?e)
  (const true))
