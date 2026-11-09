#ifndef RME_UID_EVENT_MODEL_H
#define RME_UID_EVENT_MODEL_H

#include <stdint.h>

/* Diagnostic event ordering only. Source values are NOT authenticated. */
enum uid_route { UID_NORMAL = 1, UID_PREFORK = 2 };
enum uid_event_type { UID_REQUEST = 1, UID_FORK, UID_PRECREATE, UID_SPECIALIZE, UID_CONFIRM, UID_EXIT };
enum uid_source { UID_SYSTEM = 1, UID_ZYGOTE, UID_CHILD, UID_KERNEL };
enum uid_phase { UID_RETIRED = 0, UID_EMPTY, UID_REQUESTED, UID_PRECREATED, UID_FORKED, UID_SPECIALIZED, UID_COMPLETE };

struct uid_event {
    uint64_t epoch, sequence, request, child;
    uint32_t uid;
    enum uid_event_type type;
    enum uid_source source;
};

struct uid_event_model {
    uint64_t epoch, next_sequence, request, child;
    uint32_t uid;
    enum uid_route route;
    enum uid_phase phase;
};

void uid_model_init(struct uid_event_model *m, uint64_t epoch, enum uid_route route);
int uid_model_accept(struct uid_event_model *m, const struct uid_event *event);
int uid_model_candidate(const struct uid_event_model *m, uint64_t *child, uint32_t *uid);
void uid_model_stop(struct uid_event_model *m);

#endif
