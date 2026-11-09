#include <assert.h>
#include <stdio.h>
#include <string.h>

static int pre(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
static int seq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static int env_is_ours(const char *e) { return pre(e, "AEMU64_"); }
#include "../vibrator_preload.h"

static int contains(char **env, const char *entry) {
    for (; *env; ++env) if (seq(*env, entry)) return 1;
    return 0;
}

int main(void) {
    char *normal[] = { "PATH=/system/bin", "AEMU64_VIB_PRELOAD=1", "RME_VIBRATION_SOCKET=abc", NULL };
    char *conflict[] = { "LD_PRELOAD=/vendor/lib64/existing.so", NULL };
    const char *target = "/vendor/bin/hw/vendor.qti.hardware.vibrator.service";
    assert(rme_vibrator_preload_select(target, 0, "1", normal) == 1);
    assert(rme_vibrator_preload_select(target, 0, NULL, normal) == 0);
    assert(rme_vibrator_preload_select(target, 0, "0", normal) == 0);
    assert(rme_vibrator_preload_select(target, 1, "1", normal) == 0);
    assert(rme_vibrator_preload_select("/system/bin/servicemanager", 0, "1", normal) == 0);
    assert(rme_vibrator_preload_select("/vendor/bin/hw/vendor.qti.hardware.vibrator.service.extra", 0, "1", normal) == 0);
    assert(rme_vibrator_preload_select(target, 0, "1", conflict) == -1);

    char *child[] = { "PATH=/system/bin", RME_VIBRATOR_PRELOAD_ENV,
                      "RME_VIBRATION_SOCKET=abc", "RME_VIBRATION_EPOCH=7",
                      "LD_PRELOAD=/vendor/lib64/user.so", "AEMU64_ROOT=/root", NULL };
    char *out[16];
    char **end = rme_vibrator_copy_env(out, child, 1); *end = NULL;
    assert(contains(out, "PATH=/system/bin"));
    assert(contains(out, "LD_PRELOAD=/vendor/lib64/user.so"));
    assert(!contains(out, RME_VIBRATOR_PRELOAD_ENV));
    assert(!contains(out, "RME_VIBRATION_SOCKET=abc"));
    assert(!contains(out, "RME_VIBRATION_EPOCH=7"));
    assert(!contains(out, "AEMU64_ROOT=/root"));

    end = rme_vibrator_copy_env(out, child, 0); *end = NULL;
    assert(contains(out, RME_VIBRATOR_PRELOAD_ENV));
    assert(contains(out, "RME_VIBRATION_SOCKET=abc"));
    assert(contains(out, "LD_PRELOAD=/vendor/lib64/user.so"));
    assert(!contains(out, "AEMU64_ROOT=/root"));
    puts("VIBRATOR_PRELOAD_POLICY_OK");
    return 0;
}
