;; Ingagi AM rewrite #49: gpu-barrier-redundant
;; Category: gpu-memory
;; Illustrative format; adapt to your engine parser and semantics.
(rewrite gpu-barrier-redundant
  (gpu.barrier (gpu.barrier ?x))
  (gpu.barrier ?x))
