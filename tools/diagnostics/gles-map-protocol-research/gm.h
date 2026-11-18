#ifndef GM_H
#define GM_H
#include <stddef.h>
#include <stdint.h>
struct gm;
struct gm *gm_new(int);
int gm_free(struct gm *);
int gm_map(struct gm *, unsigned, intptr_t, intptr_t, unsigned, uint64_t *,
           void **);
int gm_flush(struct gm *, unsigned, uint64_t, intptr_t, intptr_t);
int gm_unmap(struct gm *, unsigned, uint64_t);
int gm_error(struct gm *, unsigned *);
int gm_bind_test(struct gm *, unsigned, unsigned);
int gm_draw_test(struct gm *, unsigned, unsigned, uint64_t *);
#endif
