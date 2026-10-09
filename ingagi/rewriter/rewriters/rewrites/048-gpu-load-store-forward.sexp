;; Ingagi AM rewrite #48: gpu-load-store-forward
;; Category: gpu-memory
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite gpu-load-store-forward
  (gpu.load (gpu.store ?ptr ?value) ?ptr)
  ?value)
