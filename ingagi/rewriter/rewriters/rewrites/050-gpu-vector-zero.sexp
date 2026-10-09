;; Ingagi AM rewrite #50: gpu-vector-zero
;; Category: gpu-memory
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite gpu-vector-zero
  (gpu.vec.add ?v (gpu.vec.zero))
  ?v)
