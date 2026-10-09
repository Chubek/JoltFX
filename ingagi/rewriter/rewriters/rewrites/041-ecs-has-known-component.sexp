;; Ingagi AM rewrite #41: ecs-has-known-component
;; Category: ecs
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite ecs-has-known-component
  (ecs.has_component ?entity ?component-known-present)
  (const true))
