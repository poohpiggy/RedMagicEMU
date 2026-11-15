#define AEMU_NX809J_DYNAMIC_REFRESH 1
#include "../hwc.c"

/* Exercise the actual HAL entry points without starting a Guest or touching fb0. */
int hw_get_module(const char *id, const void **module) { (void)id; (void)module; return -1; }
extern int usleep(unsigned int usec);

static volatile int ticks;
static int64_t first_ns, last_ns, first_real_ns, last_real_ns;
static void record_vsync(const struct procs *p, int display, int64_t timestamp) {
    (void)p; (void)display;
    struct timespec64 now;
    clock_gettime(1, &now);
    int64_t actual = now.tv_sec * 1000000000LL + now.tv_nsec;
    if (!ticks) { first_ns = timestamp; first_real_ns = actual; }
    last_ns = timestamp;
    last_real_ns = actual;
    __atomic_add_fetch(&ticks, 1, __ATOMIC_RELEASE);
}

static int wait_ticks(int count) {
    for (int i = 0; i < 200; i++) {
        if (__atomic_load_n(&ticks, __ATOMIC_ACQUIRE) >= count) return 0;
        usleep(10000);
    }
    return 1;
}

int main(void) {
    struct hwc d = {0};
    d.w = 1216; d.h = 2688;
    const int periods[] = {16666666, 11111111, 8333333, 6944444};
    uint32_t configs[4] = {99, 99, 99, 99};
    size_t n = 4;
    if (hwc_get_configs(&d, 0, configs, &n) || n != 4) return 1;
    for (int i = 0; i < 4; i++) {
        uint32_t attrs[] = {1, 2, 3, 0};
        int32_t values[3];
        if (configs[i] != (uint32_t)i || hwc_get_attributes(&d, 0, configs[i], attrs, values)) return 2;
        if (values[0] != periods[i] || values[1] != d.w || values[2] != d.h) return 3;
        if (hwc_set_active_config(&d, 0, configs[i]) || hwc_get_active_config(&d, 0) != i) return 4;
    }
    if (hwc_set_active_config(&d, 0, 4) == 0 || hwc_get_active_config(&d, 0) != 3) return 5;
    if (hwc_set_active_config(&d, 1, 0) == 0) return 6;
    n = 2;
    if (hwc_get_configs(&d, 0, configs, &n) || n != 4 || configs[1] != 1) return 7;
    n = 0;
    if (hwc_get_configs(&d, 0, 0, &n) || n != 4) return 7;
    struct procs p = {0}; p.vsync = record_vsync;
    d.procs = &p; d.vsync_on = 1;
    long thread;
    if (pthread_create(&thread, 0, vsync_thread, &d)) return 8;
    for (int i = 0; i < 4; i++) {
        d.vsync_on = 0;
        usleep(30000);
        if (hwc_set_active_config(&d, 0, i)) return 9;
        usleep(30000);
        ticks = 0;
        d.vsync_on = 1;
        if (wait_ticks(20)) return 10;
        d.vsync_on = 0;
        usleep(30000);
        int64_t average = (last_ns - first_ns) / (ticks - 1);
        int64_t actual = (last_real_ns - first_real_ns) / (ticks - 1);
        if (average < periods[i] - 100 || average > periods[i] + 100) return 11 + i;
        if (actual < periods[i] * 8 / 10 || actual > periods[i] * 12 / 10) return 15 + i;
    }
    return 0;
}
