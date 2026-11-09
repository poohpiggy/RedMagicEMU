#ifndef RME_QTI_LED_ADAPTER_H
#define RME_QTI_LED_ADAPTER_H

#include "guest_sender.h"
#include <stddef.h>

/* Experimental only: the caller must separately authenticate the host,
 * authorize the original Guest request and serialize the underlying HAL
 * transaction. Never call these functions in a signal handler. */
struct rme_qti_led_adapter {
    pthread_mutex_t lock;
    struct rme_guest_vibration_sender *sender;
    pthread_t owner;
    unsigned state;
    unsigned duration;
    int retired;
};

int rme_qti_led_init(struct rme_qti_led_adapter *adapter, struct rme_guest_vibration_sender *sender);
int rme_qti_led_write(struct rme_qti_led_adapter *adapter, const char *path, const void *value, size_t size);
/* Call only after all users have stopped; sender lifetime remains the caller's. */
int rme_qti_led_destroy(struct rme_qti_led_adapter *adapter);
#endif
