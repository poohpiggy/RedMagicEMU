#define _GNU_SOURCE
#include "../vibration_bproto_lifecycle.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define T_BINDER 0x73622a85u
#define T_HANDLE 0x73682a85u
#define BR_INCREFS 0x80107207u
#define BR_ACQUIRE 0x80107208u
#define BR_RELEASE 0x80107209u
#define BR_DECREFS 0x8010720au

static int connect_daemon(const char *path) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    assert(strlen(path) < sizeof sa.sun_path);
    strcpy(sa.sun_path, path);
    assert(connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0);
    struct timeval timeout = { .tv_sec = 3 };
    assert(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0);
    return fd;
}
static unsigned mask_events(const unsigned char *br, size_t length) {
    unsigned mask = 0;
    for (size_t i = 0; i < length;) {
        uint32_t cmd; memcpy(&cmd, br + i, 4); i += 4;
        if (cmd == BR_INCREFS || cmd == BR_ACQUIRE || cmd == BR_RELEASE || cmd == BR_DECREFS) {
            assert(length - i >= 16); i += 16;
            if (cmd == BR_INCREFS) mask |= 1;
            if (cmd == BR_ACQUIRE) mask |= 2;
            if (cmd == BR_RELEASE) mask |= 4;
            if (cmd == BR_DECREFS) mask |= 8;
        }
    }
    return mask;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    pid_t daemon_pid = (pid_t)strtol(argv[2], NULL, 10);
    struct rme_bproto_client manager, service;
    assert(rme_bproto_open(&manager, connect_daemon(argv[1]), daemon_pid, getuid(), 1000, 201) == 0);
    assert(rme_bproto_open(&service, connect_daemon(argv[1]), daemon_pid, getuid(), 1000, 202) == 0);
    struct { struct c_hdr h; uint32_t cmd; } become = { .h = { .op = OP_BCS, .euid = 1000 }, .cmd = 0x0000633eu };
    assert(send(manager.fd, &become, sizeof become, MSG_NOSIGNAL) == sizeof become);
    struct c_hdr poll = { .op = OP_READ, .flags = F_READ | F_NONBLOCK, .read_size = 4096, .euid = 1000 };
    assert(send(manager.fd, &poll, sizeof poll, MSG_NOSIGNAL) == sizeof poll);
    struct d_hdr h; unsigned char buf[4096]; size_t length;
    assert(rme_bproto_receive(&manager, &h, buf, sizeof buf, &length) == 0 && (h.flags & F_AGAIN));
    struct rme_vib_lifecycle svc, mgr;
    const uint64_t ptr = 0x12345678, cookie = 0x55667788;
    rme_vib_lifecycle_init(&svc, &service, ptr, cookie);
    rme_vib_lifecycle_init(&mgr, &manager, 0, 0);
    assert(rme_vib_lifecycle_enter(&svc) == 0);
    assert(rme_bproto_read_request(&manager, 4096) == 0);
    struct { uint32_t type, flags; uint64_t binder, cookie; } node = {T_BINDER, 0x13, ptr, cookie};
    const uint64_t offset = 0;
    assert(rme_bproto_txn(&service, 0, 23, 0, &node, sizeof node, &offset, 1) == 0);
    assert(rme_bproto_read_request(&service, 4096) == 0);
    assert(rme_bproto_receive(&manager, &h, buf, sizeof buf, &length) == 0);
    assert((h.flags & F_TXN) && h.code == 23 && h.data_size == sizeof node && h.offsets_size == 8);
    uint32_t type; memcpy(&type, buf + h.br_size, 4); assert(type == T_HANDLE);
    assert(rme_vib_lifecycle_observe(&mgr, &h, buf, length) == 0);
    const unsigned char reply[4] = {0};
    assert(rme_bproto_reply(&manager, reply, sizeof reply) == 0);
    unsigned events = 0; int replied = 0;
    for (int count = 0; count < 12 && (!replied || (events & 3) != 3); ++count) {
        assert(rme_bproto_receive(&service, &h, buf, sizeof buf, &length) == 0);
        events |= mask_events(buf, h.br_size);
        assert(rme_vib_lifecycle_observe(&svc, &h, buf, length) == 0);
        if (h.flags & F_TXN) { assert(h.data_size == sizeof reply); replied = 1; assert(rme_vib_lifecycle_free(&svc) == 0); }
        if (!replied || (events & 3) != 3) assert(rme_bproto_read_request(&service, 4096) == 0);
    }
    assert(replied && (events & 3) == 3);
    assert(rme_vib_lifecycle_free(&mgr) == 0);
    assert(rme_bproto_read_request(&service, 4096) == 0);
    for (int count = 0; count < 12 && (events & 12) != 12; ++count) {
        assert(rme_bproto_receive(&service, &h, buf, sizeof buf, &length) == 0);
        events |= mask_events(buf, h.br_size);
        assert(rme_vib_lifecycle_observe(&svc, &h, buf, length) == 0);
        if ((events & 12) != 12) assert(rme_bproto_read_request(&service, 4096) == 0);
    }
    assert((events & 15) == 15);
    assert(rme_vib_lifecycle_exit(&svc) == 0);
    rme_bproto_close(&manager); rme_bproto_close(&service);
    puts("VIBRATION_BPROTO_LIFECYCLE_DAEMON_OK");
}
