#ifndef RME_QTI_LED_SHIM_H
#define RME_QTI_LED_SHIM_H

struct rme_qti_led_adapter;

/* Bind once per process to an already initialized adapter; this does not
 * authenticate the host or authorize the Guest request. The caller must keep
 * the adapter alive until unbind returns, and must not destroy it concurrently.
 * Unbind waits for in-flight calls and permanently retires this shim instance.
 * None of these methods is async-signal-safe. */
int rme_qti_shim_bind(struct rme_qti_led_adapter *adapter);
int rme_qti_shim_unbind(struct rme_qti_led_adapter *adapter);

#endif
