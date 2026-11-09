#include "qti_led_adapter.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>

#define QTI_LED_BASE "/sys/class/leds/zte_vibrator/"

static int literal(const void *value, size_t size, const char *text, size_t expected) {
    return size == expected && memcmp(value, text, expected) == 0;
}

static int duration_value(const void *value, size_t size, unsigned *result) {
    const unsigned char *p = value;
    if (size < 3 || size > 6 || p[size - 1] != 0 || p[size - 2] != '\n' || p[0] == '0') return 0;
    unsigned n = 0;
    for (size_t i = 0; i < size - 2; ++i) {
        if (p[i] < '0' || p[i] > '9') return 0;
        n = n * 10 + (unsigned)(p[i] - '0');
    }
    if (n == 0 || n > 5000) return 0;
    *result = n;
    return 1;
}

int rme_qti_led_init(struct rme_qti_led_adapter *adapter, struct rme_guest_vibration_sender *sender) {
    if (!adapter || !sender) return -EINVAL;
    int error = pthread_mutex_init(&adapter->lock, NULL);
    if (error) return -error;
    adapter->sender = sender;
    adapter->state = 0;
    adapter->duration = 0;
    adapter->retired = 0;
    return 0;
}
int rme_qti_led_write(struct rme_qti_led_adapter *adapter, const char *path, const void *value, size_t size) {
    if (!adapter || !path || !value || size == 0) return -EINVAL;
    int error = pthread_mutex_lock(&adapter->lock);
    if (error) return -error;
    int result = -EINVAL;
    if (adapter->retired) { result = -ESHUTDOWN; goto out; }

    /* Cancellation interrupts any pending incomplete on sequence. */
    if (strcmp(path, QTI_LED_BASE "activate_aw") == 0 && literal(value, size, "0", sizeof("0"))) {
        adapter->state = 0;
        result = rme_guest_vibration_off(adapter->sender);
        if (result) adapter->retired = 1;
        goto out;
    }
    /* A new on sequence supersedes an incomplete one. The old owner cannot
     * finish it after ownership changes, and stale duration is discarded. */
    if (strcmp(path, QTI_LED_BASE "gain") == 0 &&
        (literal(value, size, "0x32", sizeof("0x32")) || literal(value, size, "0x80", sizeof("0x80")))) {
        adapter->owner = pthread_self();
        adapter->state = 1;
        adapter->duration = 0;
        result = 0;
        goto out;
    }
    if (adapter->state && !pthread_equal(adapter->owner, pthread_self())) {
        result = -EBUSY;
        goto out;
    }
    if (adapter->state == 1 && strcmp(path, QTI_LED_BASE "activate_mode") == 0 &&
               literal(value, size, "5", sizeof("5"))) {
        adapter->state = 2;
        result = 0;
    } else if (adapter->state == 2 && strcmp(path, QTI_LED_BASE "index") == 0 &&
               literal(value, size, "4", sizeof("4"))) {
        adapter->state = 3;
        result = 0;
    } else if (adapter->state == 3 && strcmp(path, QTI_LED_BASE "duration_aw") == 0 &&
               duration_value(value, size, &adapter->duration)) {
        adapter->state = 4;
        result = 0;
    } else if (adapter->state == 4 && strcmp(path, QTI_LED_BASE "activate_aw") == 0 &&
               literal(value, size, "1", sizeof("1"))) {
        adapter->state = 0;
        result = rme_guest_vibration_on(adapter->sender, adapter->duration);
        if (result) adapter->retired = 1;
    } else {
        adapter->state = 0;
    }
out:
    pthread_mutex_unlock(&adapter->lock);
    return result;
}
int rme_qti_led_destroy(struct rme_qti_led_adapter *adapter) {
    if (!adapter) return -EINVAL;
    return -pthread_mutex_destroy(&adapter->lock);
}
