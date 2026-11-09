/* Experimental QTI interception. Never load into a Guest until authorization,
 * peer identity, startup, linker selection, and process lifetime are verified. */
#include "qti_led_shim.h"
#include "qti_led_adapter.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct rme_qti_led_adapter *current;
static int retired;

int rme_qti_shim_bind(struct rme_qti_led_adapter *adapter) {
    if (!adapter) return -EINVAL;
    int error = pthread_mutex_lock(&lock);
    if (error) return -error;
    int result = current || retired ? -EALREADY : 0;
    if (!result) current = adapter;
    pthread_mutex_unlock(&lock);
    return result;
}

int rme_qti_shim_unbind(struct rme_qti_led_adapter *adapter) {
    if (!adapter) return -EINVAL;
    int error = pthread_mutex_lock(&lock);
    if (error) return -error;
    int result = current != adapter ? -EINVAL : 0;
    if (!result) { current = NULL; retired = 1; }
    pthread_mutex_unlock(&lock);
    return result;
}
int rme_qti_shim_write(void *self, const char *path, char *value)
    __asm__("_ZN4aidl7android8hardware8vibrator17LedVibratorDevice11write_valueEPKcPc");
int rme_qti_shim_write(void *self, const char *path, char *value) {
    (void)self;
    if (!path || !value) return -EINVAL;
    static const char *const names[] = {
        "/sys/class/leds/zte_vibrator/gain",
        "/sys/class/leds/zte_vibrator/activate_mode",
        "/sys/class/leds/zte_vibrator/index",
        "/sys/class/leds/zte_vibrator/duration_aw",
        "/sys/class/leds/zte_vibrator/activate_aw"
    };
    int matched = 0;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(path, names[i]) == 0) { matched = 1; break; }
    if (!matched) return -EPERM;
    size_t size = 0;
    while (size < 64 && value[size]) ++size;
    if (size == 64) return -EINVAL;
    ++size; /* The original write_value includes its terminating NUL. */
    int error = pthread_mutex_lock(&lock);
    if (error) return -error;
    int result = current ? rme_qti_led_write(current, path, value, size) : -ENODEV;
    pthread_mutex_unlock(&lock);
    return result;
}
