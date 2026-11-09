#ifndef AEMU_BINDER_CLIENT_TRACE_H
#define AEMU_BINDER_CLIENT_TRACE_H

/* Only a bounded, complete kernel argv prefix can identify the target. */
static int bct_identity(const char *buf, unsigned long n, const char *engine) {
    static const char name[] = "com.baidu.input_oem";
    unsigned long i = 0, j = 0;
    while (engine[i]) { if (i >= n || buf[i] != engine[i]) return 0; i++; }
    if (!i || i >= n || buf[i++] != 0) return 0;
    while (name[j]) { if (i >= n || buf[i++] != name[j++]) return 0; }
    return i < n && buf[i] == 0;
}

static int bct_thread(const char *buf, unsigned long n) {
    static const char name[] = "fin-1-thread-4\n";
    if (n != sizeof name - 1) return 0;
    for (unsigned long i = 0; i < n; i++) if (buf[i] != name[i]) return 0;
    return 1;
}

static int bct_reserve(unsigned *count) {
    unsigned cur = __atomic_load_n(count, __ATOMIC_RELAXED);
    while (cur < 128) {
        if (__atomic_compare_exchange_n(count, &cur, cur + 1, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return 1;
    }
    return 0;
}

/* No guest strings, buffers, paths or Binder parcels enter this formatter. */
static char *bct_text(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *bct_number(char *p, unsigned long n) {
    char tmp[24]; unsigned i = 0;
    do { tmp[i++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (i) *p++ = tmp[--i];
    return p;
}
static unsigned long bct_format(char *out, unsigned long pid, unsigned long tid,
                                unsigned long fd, int driver, int phase) {
    char *p = out;
    p = bct_text(p, "binder-client pid="); p = bct_number(p, pid);
    p = bct_text(p, " tid="); p = bct_number(p, tid);
    p = bct_text(p, " fd="); p = bct_number(p, fd);
    p = bct_text(p, " driver=");
    p = bct_text(p, driver == 1 ? "main" : driver == 2 ? "hw" : driver == 3 ? "vnd" : "unknown");
    p = bct_text(p, " phase=");
    p = bct_text(p, phase == 0 ? "wait" : phase == 1 ? "recv" : "error");
    *p++ = '\n';
    return (unsigned long)(p - out);
}
#endif
