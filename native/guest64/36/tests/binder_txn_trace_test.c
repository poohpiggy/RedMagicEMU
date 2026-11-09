/* Run without a C runtime as well as on Android: exit status reports failure. */
#if __has_include("../binder_txn_trace.h")
#include "../binder_txn_trace.h"
#else
struct btt_state { unsigned next_id, events; };
static int btt_identity(const char *b, unsigned long n, const char *e) { (void)b; (void)n; (void)e; return 0; }
static int btt_thread(const char *b, unsigned long n) { (void)b; (void)n; return 0; }
static unsigned btt_start(struct btt_state *s, int enabled, int identity, int thread, unsigned flags) { (void)s; (void)enabled; (void)identity; (void)thread; (void)flags; return 0; }
static int btt_event(struct btt_state *s, unsigned id) { (void)s; (void)id; return 0; }
#endif

int main(void) {
    static const char cmd[] = "/data/app/libaemu64_36.so\0com.baidu.input_oem\0";
    static const char sub[] = "/data/app/libaemu64_36.so\0com.baidu.input_oem:other\0";
    static const char comm[] = "fin-1-thread-4\n";
    struct btt_state s = {0};
    if (!btt_identity(cmd, sizeof cmd, "/data/app/libaemu64_36.so")) return 1;
    if (btt_identity(cmd, sizeof cmd - 2, "/data/app/libaemu64_36.so")) return 2;
    if (btt_identity(sub, sizeof sub, "/data/app/libaemu64_36.so")) return 3;
    if (!btt_thread(comm, sizeof comm - 1) || btt_thread(comm, sizeof comm - 2)) return 4;
    if (btt_start(&s, 0, 1, 1, 0) || btt_start(&s, 1, 0, 1, 0) ||
        btt_start(&s, 1, 1, 0, 0) || btt_start(&s, 1, 1, 1, 1)) return 5;
    unsigned a = btt_start(&s, 1, 1, 1, 0), b = btt_start(&s, 1, 1, 1, 0);
    if (!a || !b || a == b || btt_event(&s, 0)) return 6;
    for (unsigned i = 0; i < 256; i++) if (!btt_event(&s, i & 1 ? a : b)) return 7;
    if (btt_event(&s, a) || btt_start(&s, 1, 1, 1, 0)) return 8;
    return 0;
}
