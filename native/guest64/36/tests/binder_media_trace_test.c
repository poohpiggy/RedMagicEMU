#include <stddef.h>
#include <string.h>
#include "../binder_txn_trace.h"

#ifndef BMT_DEFINED
static int bmt_identity(const char *b, unsigned long n, const char *e) {
    (void)b; (void)n; (void)e; return 0;
}
#endif
#ifndef BMT_HWM_DEFINED
static int bmt_hwmanager_identity(const char *b, unsigned long n, const char *e) {
    (void)b; (void)n; (void)e; return 0;
}
#endif
#ifndef BMT_LIMIT
#define BMT_LIMIT 4096u
static unsigned bmt_start(struct btt_state *s, int enabled, int identity, unsigned flags) {
    return btt_start(s, enabled, identity, 1, flags);
}
static int bmt_event(struct btt_state *s, unsigned id) { return btt_event(s, id); }
#endif

int main(void) {
    static const char engine[] = "/data/app/libaemu64_36.so";
    static const char valid[] = "/data/app/libaemu64_36.so\0/system/bin/mediaserver\0/system/bin/mediaserver\0";
    static const char other[] = "/data/app/libaemu64_36.so\0/system/bin/mediaserver:other\0/system/bin/mediaserver\0";
    static const char other2[] = "/data/app/libaemu64_36.so\0/system/bin/mediaserver\0/system/bin/other\0";
    static const char hw[] = "/data/app/libaemu64_36.so\0/system/bin/hwservicemanager\0/system/bin/hwservicemanager\0";
    static const char hw_other[] = "/data/app/libaemu64_36.so\0/system/bin/hwservicemanager\0/system/bin/hwservicemanager2\0";
    if (!bmt_identity(valid, sizeof valid - 1, engine)) return 1;
    if (bmt_identity(valid, sizeof valid - 2, engine)) return 2;
    if (bmt_identity(other, sizeof other - 1, engine)) return 3;
    if (bmt_identity(other2, sizeof other2 - 1, engine)) return 4;
    if (bmt_identity(valid, sizeof valid - 1, "/data/app/other.so")) return 5;
    if (bmt_identity(valid, sizeof valid - 1, "")) return 6;
    if (!bmt_hwmanager_identity(hw, sizeof hw - 1, engine)) return 11;
    if (bmt_hwmanager_identity(hw, sizeof hw - 2, engine) ||
        bmt_hwmanager_identity(hw_other, sizeof hw_other - 1, engine) ||
        bmt_hwmanager_identity(valid, sizeof valid - 1, engine)) return 12;
    struct btt_state state = { .events = 255 };
    unsigned id = bmt_start(&state, 1, 1, 0);
    if (!id || !bmt_event(&state, id) || state.events != 256) return 7;
    if (!bmt_event(&state, id) || state.events != 257) return 8;
    state.events = BMT_LIMIT - 1;
    if (!bmt_event(&state, id) || state.events != BMT_LIMIT) return 9;
    if (bmt_event(&state, id) || bmt_start(&state, 1, 1, 0)) return 10;
    return 0;
}
