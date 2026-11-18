/* SPDX-License-Identifier: MIT
 * A deliberately limited single-threaded diagnostic, not product code. */
#include "model.h"
#include <stdlib.h>

struct model_object { uint32_t name; uint64_t generation; int size, refs; struct model_object *next; };
struct model_group { int contexts; uint64_t next_generation; struct model_object *names; };
struct model_context { struct model_group *group; struct model_object *bound[2]; };

static struct model_object *lookup(struct model_group *g, uint32_t name) {
    for (struct model_object *o = g->names; o; o = o->next) if (o->name == name) return o;
    return NULL;
}
static void drop(struct model_object *o) { if (o && --o->refs == 0) free(o); }
struct model_group *model_group_new(void) { return calloc(1, sizeof(struct model_group)); }
struct model_context *model_context_new(struct model_group *g) {
    struct model_context *c = calloc(1, sizeof(*c));
    if (c) { c->group = g; ++g->contexts; }
    return c;
}
void model_context_destroy(struct model_context *c) {
    struct model_group *g = c->group;
    for (int i = 0; i < 2; ++i) drop(c->bound[i]);
    free(c);
    if (--g->contexts == 0) {
        while (g->names) { struct model_object *o = g->names; g->names = o->next; drop(o); }
        free(g);
    }
}
int model_generate(struct model_group *g, uint32_t name, int size) {
    if (!name || lookup(g, name)) return 0;
    struct model_object *o = calloc(1, sizeof(*o));
    if (!o) return 0;
    o->name = name; o->size = size; o->refs = 1; o->generation = ++g->next_generation;
    o->next = g->names; g->names = o;
    return 1;
}
int model_bind(struct model_context *c, int slot, uint32_t name) {
    if (slot < 0 || slot >= 2) return 0;
    struct model_object *o = name ? lookup(c->group, name) : NULL;
    if (name && !o) return 0;
    if (o) ++o->refs;
    drop(c->bound[slot]); c->bound[slot] = o;
    return 1;
}
void model_delete(struct model_context *c, uint32_t name) {
    struct model_object **link = &c->group->names;
    while (*link && (*link)->name != name) link = &(*link)->next;
    if (!*link) return;
    struct model_object *o = *link;
    *link = o->next;
    for (int i = 0; i < 2; ++i) if (c->bound[i] == o) { c->bound[i] = NULL; drop(o); }
    drop(o);
}
const struct model_object *model_bound(struct model_context *c, int slot) { return c->bound[slot]; }
uint32_t model_name(const struct model_object *o) { return o ? o->name : 0; }
uint64_t model_generation(const struct model_object *o) { return o ? o->generation : 0; }
int model_size(const struct model_object *o) { return o ? o->size : 0; }
