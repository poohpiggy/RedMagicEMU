#include "uid_event_model.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static struct uid_event event(uint64_t epoch, uint64_t sequence, enum uid_event_type type,
                              enum uid_source source, uint64_t request, uint64_t child, uint32_t uid) {
    return (struct uid_event){ epoch, sequence, request, child, uid, type, source };
}
static void accept(struct uid_event_model *m, struct uid_event e) { assert(uid_model_accept(m, &e) == 1); }
static void unavailable(struct uid_event_model *m) {
    uint64_t child = 17; uint32_t uid = 23;
    assert(!uid_model_candidate(m, &child, &uid));
    assert(child == 17 && uid == 23);
}
static void normal_until_specialized(struct uid_event_model *m) {
    uid_model_init(m, 7, UID_NORMAL);
    unavailable(m);
    accept(m, event(7, 1, UID_REQUEST, UID_SYSTEM, 9, 0, 10001));
    unavailable(m);
    accept(m, event(7, 2, UID_FORK, UID_ZYGOTE, 9, 31, 0));
    unavailable(m);
    accept(m, event(7, 3, UID_SPECIALIZE, UID_CHILD, 9, 31, 10001));
    unavailable(m);
}
static void expect_rejected(struct uid_event_model *m, struct uid_event e) {
    assert(!uid_model_accept(m, &e)); unavailable(m);
    assert(!uid_model_accept(m, &e)); unavailable(m);
}
int main(void) {
    struct uid_event_model m;
    uint64_t child; uint32_t uid;
    normal_until_specialized(&m);
    accept(&m, event(7, 4, UID_CONFIRM, UID_ZYGOTE, 9, 31, 10001));
    assert(uid_model_candidate(&m, &child, &uid) && child == 31 && uid == 10001);
    accept(&m, event(7, 5, UID_EXIT, UID_KERNEL, 9, 31, 0));
    unavailable(&m);

    uid_model_init(&m, 8, UID_PREFORK);
    accept(&m, event(8, 1, UID_PRECREATE, UID_ZYGOTE, 0, 32, 0));
    unavailable(&m);
    accept(&m, event(8, 2, UID_REQUEST, UID_SYSTEM, 10, 32, 10002));
    unavailable(&m);
    accept(&m, event(8, 3, UID_SPECIALIZE, UID_CHILD, 10, 32, 10002));
    unavailable(&m);
    accept(&m, event(8, 4, UID_CONFIRM, UID_ZYGOTE, 10, 32, 10002));
    assert(uid_model_candidate(&m, &child, &uid) && child == 32 && uid == 10002);
    uid_model_stop(&m); unavailable(&m);
    expect_rejected(&m, event(8, 5, UID_CONFIRM, UID_ZYGOTE, 10, 32, 10002));

    uid_model_init(&m, 7, UID_NORMAL);
    expect_rejected(&m, event(7, 1, UID_FORK, UID_ZYGOTE, 9, 31, 0));
    uid_model_init(&m, 7, UID_NORMAL);
    accept(&m, event(7, 1, UID_REQUEST, UID_SYSTEM, 9, 0, 10001));
    expect_rejected(&m, event(7, 1, UID_REQUEST, UID_SYSTEM, 9, 0, 10001));
    normal_until_specialized(&m);
    expect_rejected(&m, event(6, 4, UID_CONFIRM, UID_ZYGOTE, 9, 31, 10001));
    normal_until_specialized(&m);
    expect_rejected(&m, event(7, 4, UID_CONFIRM, UID_CHILD, 9, 31, 10001));
    normal_until_specialized(&m);
    expect_rejected(&m, event(7, 4, UID_CONFIRM, UID_ZYGOTE, 9, 31, 10002));
    normal_until_specialized(&m);
    expect_rejected(&m, event(7, 4, UID_CONFIRM, UID_ZYGOTE, 9, 32, 10001));
    normal_until_specialized(&m);
    expect_rejected(&m, event(7, 5, UID_CONFIRM, UID_ZYGOTE, 9, 31, 10001));
    normal_until_specialized(&m);
    uid_model_stop(&m); unavailable(&m);
    expect_rejected(&m, event(7, 4, UID_CONFIRM, UID_ZYGOTE, 9, 31, 10001));

    uid_model_init(&m, 0, UID_NORMAL);
    expect_rejected(&m, event(0, 1, UID_REQUEST, UID_SYSTEM, 9, 0, 10001));
    uid_model_init(&m, 7, (enum uid_route)99);
    expect_rejected(&m, event(7, 1, UID_REQUEST, UID_SYSTEM, 9, 0, 10001));
    uid_model_init(&m, 7, UID_NORMAL);
    expect_rejected(&m, event(7, 1, UID_REQUEST, UID_SYSTEM, 0, 0, 10001));
    uid_model_init(&m, 7, UID_NORMAL);
    expect_rejected(&m, event(7, 1, UID_REQUEST, UID_SYSTEM, 9, 0, UINT32_MAX));
    uid_model_init(&m, 7, UID_NORMAL);
    accept(&m, event(7, 1, UID_REQUEST, UID_SYSTEM, 9, 0, 10001));
    expect_rejected(&m, event(7, 2, UID_FORK, UID_ZYGOTE, 10, 31, 0));
    uid_model_init(&m, 7, UID_NORMAL);
    accept(&m, event(7, 1, UID_REQUEST, UID_SYSTEM, 9, 0, 10001));
    expect_rejected(&m, event(7, 2, UID_FORK, UID_SYSTEM, 9, 31, 0));
    uid_model_init(&m, 7, UID_NORMAL);
    accept(&m, event(7, 1, UID_REQUEST, UID_SYSTEM, 9, 0, 10001));
    accept(&m, event(7, 2, UID_FORK, UID_ZYGOTE, 9, 31, 0));
    expect_rejected(&m, event(7, 3, UID_SPECIALIZE, UID_CHILD, 9, 31, 10002));
    uid_model_init(&m, 7, UID_NORMAL);
    accept(&m, event(7, 1, UID_REQUEST, UID_SYSTEM, 9, 0, 10001));
    accept(&m, event(7, 2, UID_FORK, UID_ZYGOTE, 9, 31, 0));
    expect_rejected(&m, event(7, 3, UID_SPECIALIZE, UID_ZYGOTE, 9, 31, 10001));
    uid_model_init(&m, 8, UID_PREFORK);
    accept(&m, event(8, 1, UID_PRECREATE, UID_ZYGOTE, 0, 32, 0));
    accept(&m, event(8, 2, UID_EXIT, UID_KERNEL, 0, 32, 0));
    unavailable(&m);
    expect_rejected(&m, event(8, 3, UID_REQUEST, UID_SYSTEM, 10, 32, 10002));
    normal_until_specialized(&m);
    m.next_sequence = UINT64_MAX;
    expect_rejected(&m, event(7, UINT64_MAX, UID_CONFIRM, UID_ZYGOTE, 9, 31, 10001));
    normal_until_specialized(&m);
    accept(&m, event(7, 4, UID_CONFIRM, UID_ZYGOTE, 9, 31, 10001));
    assert(!uid_model_candidate(&m, NULL, &uid));
    assert(!uid_model_candidate(&m, &child, NULL));
    expect_rejected(&m, event(7, 5, UID_CONFIRM, UID_ZYGOTE, 9, 31, 10001));
    uid_model_init(&m, 8, UID_PREFORK);
    accept(&m, event(8, 1, UID_PRECREATE, UID_ZYGOTE, 0, 32, 0));
    expect_rejected(&m, event(8, 2, UID_REQUEST, UID_SYSTEM, 10, 33, 10002));
    puts("UID_EVENT_MODEL_OK");
}
