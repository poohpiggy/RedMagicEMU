#define _GNU_SOURCE
#include "../qti_led_adapter.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BASE "/sys/class/leds/zte_vibrator/"
#define W(adapter, node, value) rme_qti_led_write((adapter), BASE node, value, sizeof(value))

static void frame(int fd, unsigned seq, unsigned op, unsigned duration) {
    unsigned char b[28];
    assert(recv(fd, b, sizeof b, 0) == sizeof b);
    assert(memcmp(b, "RME1", 4) == 0 && b[11] == 17 && b[19] == seq);
    assert(b[20] == 1 && b[21] == 0 && b[22] == 5 && b[23] == op);
    assert((((unsigned)b[24] << 24) | ((unsigned)b[25] << 16) |
            ((unsigned)b[26] << 8) | b[27]) == duration);
}

static void no_frame(int fd) {
    char b[28];
    assert(recv(fd, b, sizeof b, MSG_DONTWAIT) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
}

static void begin(struct rme_qti_led_adapter *a, const char *duration, size_t size) {
    assert(W(a, "gain", "0x32") == 0);
    assert(W(a, "activate_mode", "5") == 0);
    assert(W(a, "index", "4") == 0);
    assert(rme_qti_led_write(a, BASE "duration_aw", duration, size) == 0);
}

static void *other_thread(void *arg) {
    struct rme_qti_led_adapter *a = arg;
    assert(W(a, "activate_mode", "5") == -EBUSY);
    return NULL;
}

static void *cancel_thread(void *arg) {
    struct rme_qti_led_adapter *a = arg;
    assert(W(a, "activate_aw", "0") == 0);
    return NULL;
}

static void *abandon_thread(void *arg) {
    struct rme_qti_led_adapter *a = arg;
    assert(W(a, "gain", "0x32") == 0);
    return NULL;
}

struct restart_args { struct rme_qti_led_adapter *adapter; pthread_barrier_t barrier; };
static void *restart_thread(void *arg) {
    struct restart_args *args = arg;
    struct rme_qti_led_adapter *a = args->adapter;
    assert(W(a, "gain", "0x80") == 0);
    pthread_barrier_wait(&args->barrier);
    pthread_barrier_wait(&args->barrier);
    assert(W(a, "activate_mode", "5") == 0);
    assert(W(a, "index", "4") == 0);
    assert(W(a, "duration_aw", "250\n") == 0);
    assert(W(a, "activate_aw", "1") == 0);
    return NULL;
}

static void test_restart_after_abandoned_sequence(void) {
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    struct rme_guest_vibration_sender sender;
    assert(rme_guest_vibration_init(&sender, pair[0], 17) == 0);
    struct rme_qti_led_adapter adapter;
    assert(rme_qti_led_init(&adapter, &sender) == 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, abandon_thread, &adapter) == 0);
    assert(pthread_join(thread, NULL) == 0);
    begin(&adapter, "500\n", sizeof("500\n"));
    assert(W(&adapter, "activate_aw", "1") == 0);
    frame(pair[1], 1, 1, 500);

    /* A newer complete request may supersede an unfinished one, but the old
     * thread cannot finish the newer request or leak its duration into it. */
    assert(W(&adapter, "gain", "0x32") == 0);
    struct restart_args args = { .adapter = &adapter };
    assert(pthread_barrier_init(&args.barrier, NULL, 2) == 0);
    assert(pthread_create(&thread, NULL, restart_thread, &args) == 0);
    pthread_barrier_wait(&args.barrier);
    assert(W(&adapter, "activate_mode", "5") == -EBUSY);
    no_frame(pair[1]);
    pthread_barrier_wait(&args.barrier);
    assert(pthread_join(thread, NULL) == 0);
    assert(pthread_barrier_destroy(&args.barrier) == 0);
    frame(pair[1], 2, 1, 250);
    assert(rme_qti_led_destroy(&adapter) == 0);
    assert(rme_guest_vibration_close(&sender) == 0);
    assert(rme_guest_vibration_destroy(&sender) == 0);
    close(pair[1]);
}

int main(void) {
    test_restart_after_abandoned_sequence();
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0);
    struct rme_guest_vibration_sender sender;
    assert(rme_guest_vibration_init(&sender, pair[0], 17) == 0);
    struct rme_qti_led_adapter a;
    assert(rme_qti_led_init(&a, &sender) == 0);

    begin(&a, "500\n", sizeof("500\n"));
    no_frame(pair[1]);
    assert(W(&a, "activate_aw", "1") == 0); frame(pair[1], 1, 1, 500);
    assert(W(&a, "activate_aw", "0") == 0); frame(pair[1], 2, 2, 0);

    assert(W(&a, "gain", "0x80") == 0);
    pthread_t thread; assert(pthread_create(&thread, NULL, other_thread, &a) == 0);
    assert(pthread_join(thread, NULL) == 0);
    assert(W(&a, "activate_mode", "5") == 0);
    assert(W(&a, "index", "4") == 0);
    assert(W(&a, "duration_aw", "5000\n") == 0);
    assert(W(&a, "activate_aw", "1") == 0); frame(pair[1], 3, 1, 5000);
    assert(W(&a, "activate_aw", "0") == 0); frame(pair[1], 4, 2, 0);

    assert(W(&a, "activate_aw", "1") == -EINVAL);
    assert(W(&a, "gain", "0x32") == 0);
    assert(W(&a, "activate_mode", "5") == 0);
    assert(W(&a, "index", "4") == 0);
    assert(W(&a, "duration_aw", "5001\n") == -EINVAL);
    assert(W(&a, "activate_aw", "1") == -EINVAL);
    assert(W(&a, "gain", "0x32") == 0);
    assert(W(&a, "activate_mode", "5") == 0);
    assert(W(&a, "index", "4") == 0);
    assert(W(&a, "duration_aw", "0\n") == -EINVAL);
    assert(rme_qti_led_write(&a, BASE "gain", "0x32", 4) == -EINVAL);
    assert(W(&a, "gain", "0x32") == 0);
    assert(pthread_create(&thread, NULL, cancel_thread, &a) == 0);
    assert(pthread_join(thread, NULL) == 0);
    frame(pair[1], 5, 2, 0);
    assert(W(&a, "activate_mode", "5") == -EINVAL);
    assert(W(&a, "gain/else", "0x32") == -EINVAL);
    no_frame(pair[1]);

    close(pair[1]);
    begin(&a, "1\n", sizeof("1\n"));
    assert(W(&a, "activate_aw", "1") < 0);
    assert(W(&a, "gain", "0x32") == -ESHUTDOWN);
    assert(rme_qti_led_destroy(&a) == 0);
    assert(rme_guest_vibration_close(&sender) == 0);
    assert(rme_guest_vibration_destroy(&sender) == 0);
    puts("QTI_LED_ADAPTER_OK");
}
