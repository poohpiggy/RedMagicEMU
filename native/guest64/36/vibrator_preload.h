#ifndef RME_VIBRATOR_PRELOAD_H
#define RME_VIBRATOR_PRELOAD_H
#define RME_VIBRATOR_PRELOAD_ENV "LD_PRELOAD=/vendor/lib64/libredmagic_vibrator.so"
static int rme_vibrator_preload_select(const char *exe, int is32, const char *option, char *const envp[]) {
    if (is32 || !exe || !option || !seq(option, "1") ||
        !seq(exe, "/vendor/bin/hw/vendor.qti.hardware.vibrator.service")) return 0;
    for (; envp && *envp; ++envp) if (pre(*envp, "LD_PRELOAD=")) return -1;
    return 1;
}
static char **rme_vibrator_copy_env(char **out, char *const envp[], int strip_vibration) {
    for (; envp && *envp; ++envp) {
        if (env_is_ours(*envp)) continue;
        if (strip_vibration && (seq(*envp, RME_VIBRATOR_PRELOAD_ENV) || pre(*envp, "RME_VIBRATION_"))) continue;
        *out++ = *envp;
    }
    return out;
}
#endif
