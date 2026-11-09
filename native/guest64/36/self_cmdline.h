#ifndef AEMU_SELF_CMDLINE_H
#define AEMU_SELF_CMDLINE_H
/* Accept only explicit aliases for the caller's own cmdline. */
static int cmdline_matches_self(const char *p, unsigned long pid) {
    const char prefix[] = "/proc/";
    unsigned long i = 0, value = 0;
    while (prefix[i]) { if (p[i] != prefix[i]) return 0; i++; }
    if (p[i] == 's') {
        const char self[] = "self/cmdline";
        unsigned long j = 0;
        while (self[j]) { if (p[i++] != self[j++]) return 0; }
        return !p[i];
    }
    if (p[i] < '1' || p[i] > '9') return 0;
    while (p[i] >= '0' && p[i] <= '9') {
        unsigned long digit = (unsigned long)(p[i++] - '0');
        if (value > (~0UL - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    const char suffix[] = "/cmdline";
    unsigned long j = 0;
    while (suffix[j]) { if (p[i++] != suffix[j++]) return 0; }
    return !p[i] && value == pid;
}
/* Zero means malformed, incomplete, empty, or not our engine. */
static unsigned long cmdline_guest_offset(const char *data, unsigned long len, const char *engine) {
    unsigned long i = 0;
    if (!len || data[len - 1]) return 0;
    while (engine[i]) {
        if (i >= len || data[i] != engine[i]) return 0;
        i++;
    }
    if (!i || i + 1 >= len || data[i] || !data[i + 1]) return 0;
    return i + 1;
}
#endif
