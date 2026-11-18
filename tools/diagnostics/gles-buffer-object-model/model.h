/* Diagnostic only: a reference model for shared buffer names and two bindings. */
#ifndef RME_BUFFER_MODEL_H
#define RME_BUFFER_MODEL_H
#include <stdint.h>
struct model_group;
struct model_context;
struct model_object;
struct model_group *model_group_new(void);
struct model_context *model_context_new(struct model_group *g);
void model_context_destroy(struct model_context *c);
int model_generate(struct model_group *g, uint32_t name, int size);
int model_bind(struct model_context *c, int slot, uint32_t name);
void model_delete(struct model_context *c, uint32_t name);
const struct model_object *model_bound(struct model_context *c, int slot);
uint32_t model_name(const struct model_object *o);
uint64_t model_generation(const struct model_object *o);
int model_size(const struct model_object *o);
#endif
