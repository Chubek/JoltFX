;; Ingagi AM rewrite #42: ecs-remove-absent
;; Category: ecs
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite ecs-remove-absent
  (ecs.remove_component ?entity ?component-known-absent)
  (unit))
