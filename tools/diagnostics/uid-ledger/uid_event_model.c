#include "uid_event_model.h"
#include <stdint.h>
#include <string.h>

void uid_model_init(struct uid_event_model *m, uint64_t epoch, enum uid_route route) {
    if (!m) return;
    memset(m, 0, sizeof *m);
    if (!epoch || (route != UID_NORMAL && route != UID_PREFORK)) return;
    m->epoch = epoch; m->route = route; m->next_sequence = 1; m->phase = UID_EMPTY;
}

void uid_model_stop(struct uid_event_model *m) {
    if (m) memset(m, 0, sizeof *m);
}

int uid_model_accept(struct uid_event_model *m, const struct uid_event *e) {
    if (!m) return 0;
    if (!e || m->phase == UID_RETIRED || e->epoch != m->epoch ||
        m->next_sequence == UINT64_MAX || e->sequence != m->next_sequence) goto reject;
    if (e->type == UID_EXIT) {
        if (!m->child || e->source != UID_KERNEL || e->request != m->request ||
            e->child != m->child || e->uid != 0) goto reject;
        uid_model_stop(m);
        return 1;
    }
    switch (m->phase) {
    case UID_EMPTY:
        if (m->route == UID_NORMAL && e->type == UID_REQUEST && e->source == UID_SYSTEM &&
            e->request && !e->child && e->uid != UINT32_MAX) {
            m->request = e->request; m->uid = e->uid; m->phase = UID_REQUESTED;
        } else if (m->route == UID_PREFORK && e->type == UID_PRECREATE &&
                   e->source == UID_ZYGOTE && !e->request && e->child && !e->uid) {
            m->child = e->child; m->phase = UID_PRECREATED;
        } else goto reject;
        break;
    case UID_REQUESTED:
        if (m->route != UID_NORMAL || e->type != UID_FORK || e->source != UID_ZYGOTE ||
            e->request != m->request || !e->child || e->uid) goto reject;
        m->child = e->child; m->phase = UID_FORKED;
        break;
    case UID_PRECREATED:
        if (m->route != UID_PREFORK || e->type != UID_REQUEST || e->source != UID_SYSTEM ||
            !e->request || e->child != m->child || e->uid == UINT32_MAX) goto reject;
        m->request = e->request; m->uid = e->uid; m->phase = UID_FORKED;
        break;
    case UID_FORKED:
        if (e->type != UID_SPECIALIZE || e->source != UID_CHILD ||
            e->request != m->request || e->child != m->child || e->uid != m->uid) goto reject;
        m->phase = UID_SPECIALIZED;
        break;
    case UID_SPECIALIZED:
        if (e->type != UID_CONFIRM || e->source != UID_ZYGOTE ||
            e->request != m->request || e->child != m->child || e->uid != m->uid) goto reject;
        m->phase = UID_COMPLETE;
        break;
    default:
        goto reject;
    }
    ++m->next_sequence;
    return 1;
reject:
    uid_model_stop(m);
    return 0;
}

int uid_model_candidate(const struct uid_event_model *m, uint64_t *child, uint32_t *uid) {
    if (!m || !child || !uid || m->phase != UID_COMPLETE) return 0;
    *child = m->child; *uid = m->uid; return 1;
}
