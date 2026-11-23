#ifndef WIIU_JIT_H
#define WIIU_JIT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	void *area;     /* start of the codegen area, 0 when the OS does not lend one */
	unsigned size;  /* its size in bytes */
	int core;       /* the only CPU core allowed to use it */
	int mode;       /* current OSCodegenSecMode */
} WiiuJitInfo;

/* What the OS offers, for the diagnostics screen. */
void wiiu_jit_query(WiiuJitInfo *info);

/* 1 when the recompiler can run here. */
int wiiu_jit_available(void);

/* Memory for the recompiler's code cache (executable), or 0 when there is none. */
void *wiiu_jit_cache(unsigned size);

#ifdef __cplusplus
}
#endif

#endif
