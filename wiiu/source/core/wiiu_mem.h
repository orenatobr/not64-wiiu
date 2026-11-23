#ifndef WIIU_MEM_H
#define WIIU_MEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* Allocates (once) the arena behind the MEM2_* regions. Returns 0 on success. */
int  wiiu_mem2_init(void);
/* Clears the arena, as the emulated machine would be after power-on. */
void wiiu_mem2_reset(void);
void wiiu_mem2_free(void);

#ifdef __cplusplus
}
#endif

#endif
