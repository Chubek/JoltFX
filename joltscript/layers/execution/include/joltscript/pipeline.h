#ifndef JOLT_PIPELINE_H
#define JOLT_PIPELINE_H
#include "joltscript/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct jolt_pipeline jolt_pipeline_t;
typedef struct jolt_budget jolt_budget_t;
/* Context-local budgets; externally synchronize if shared. */
jolt_budget_t *jolt_budget_create(size_t limit);
void jolt_budget_destroy(jolt_budget_t *);
jolt_status_t jolt_budget_acquire(jolt_budget_t *, size_t bytes);
void jolt_budget_release(jolt_budget_t *, size_t bytes);
size_t jolt_budget_used(const jolt_budget_t *);
/* Stages form a DAG of RGBA image transforms. Each stage reads one predecessor
 * (-1 reads the source), plus copied uniform parameters. Programs are copied.
 * Stages may refer forward; execution validates references and detects cycles. */
jolt_pipeline_t *jolt_pipeline_create(size_t memory_limit);
void jolt_pipeline_destroy(jolt_pipeline_t *);
jolt_status_t jolt_pipeline_add(jolt_pipeline_t *, const uint8_t *code, size_t code_size,
    int predecessor, const float *parameters, size_t parameter_count, size_t *out_stage);
jolt_status_t jolt_pipeline_run(jolt_pipeline_t *, const float *rgba, size_t pixels,
    size_t output_stage, float *out_rgba);
#ifdef __cplusplus
}
#endif
#endif
