#define _GNU_SOURCE
#include "../vibration_bproto_client.h"
#include "../vibration_service_registration.h"
#include "../bproto.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

static int connect_daemon(const char *path) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(fd >= 0);
    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    assert(strlen(path) < sizeof sa.sun_path);
    strcpy(sa.sun_path, path);
    assert(connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0);
    struct timeval timeout = { .tv_sec = 3 };
    assert(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0);
    return fd;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    pid_t daemon_pid = (pid_t)strtol(argv[2], NULL, 10);
    struct rme_bproto_client manager, caller;
    assert(rme_bproto_open(&manager, connect_daemon(argv[1]), daemon_pid, getuid(), 1000, 101) == 0);
    assert(rme_bproto_open(&caller, connect_daemon(argv[1]), daemon_pid, getuid(), 1000, 102) == 0);
    /* The fake manager is a test fixture; the production client never claims context manager. */
    struct { struct c_hdr h; uint32_t cmd; } become = { .h = { .op = OP_BCS, .euid = 1000 }, .cmd = 0x0000633eu };
    assert(send(manager.fd, &become, sizeof become, MSG_NOSIGNAL) == sizeof become);
    struct c_hdr poll = { .op = OP_READ, .flags = F_READ | F_NONBLOCK, .read_size = 4096, .euid = 1000 };
    assert(send(manager.fd, &poll, sizeof poll, MSG_NOSIGNAL) == sizeof poll);
    struct d_hdr response; unsigned char buf[512]; size_t length;
    assert(rme_bproto_receive(&manager, &response, buf, sizeof buf, &length) == 0 && (response.flags & F_AGAIN));
    assert(rme_bproto_read_request(&manager, 4096) == 0);
    const char request[8] = {1,2,3,4,5,6,7,8};
    assert(rme_bproto_txn(&caller, 0, 23, 0, request, sizeof request, NULL, 0) == 0);
    assert(rme_bproto_read_request(&caller, 4096) == 0);
    assert(rme_bproto_receive(&manager, &response, buf, sizeof buf, &length) == 0);
    assert((response.flags & F_TXN) && response.code == 23 && response.data_size == sizeof request);
    assert(length >= response.br_size + sizeof request && !memcmp(buf + response.br_size, request, sizeof request));
    const char reply[8] = {8,7,6,5,4,3,2,1};
    assert(rme_bproto_reply(&manager, reply, sizeof reply) == 0);
    assert(rme_bproto_receive(&caller, &response, buf, sizeof buf, &length) == 0);
    assert((response.flags & F_TXN) && response.data_size == sizeof reply);
    assert(length >= response.br_size + sizeof reply && !memcmp(buf + response.br_size, reply, sizeof reply));

    /* Synthetic manager and Binder owner. Metadata comes from the Guest's
     * VINTF-marked Java Binder diagnostic; the target-name offset is inferred. */
    struct rme_vib_target_registration_input registration = {
        .model="NX809J", .abi="arm64-v8a", .api=36, .n64=1, .experimental_enabled=1,
        .transaction = { .interface_header = {0,0,0,0,0,0,0,0,'T','S','Y','S'},
            .binder_flags = 0x113, .binder_ptr = 0x1000, .binder_cookie = 0x2000, .stability = 63 },
    };
    unsigned char parcel[200]; size_t parcel_len; uint64_t candidate;
    assert(rme_vib_encode_target_add_service(&registration, parcel, sizeof parcel, &parcel_len, &candidate) == 0);
    assert(rme_bproto_txn(&caller, 0, 5, 0x10, parcel, parcel_len, &candidate, 1) == 0);
    assert(rme_bproto_read_request(&caller, 4096) == 0);
    for (int i = 0; ; ++i) {
        assert(i < 4 && rme_bproto_read_request(&manager, 4096) == 0);
        assert(rme_bproto_receive(&manager, &response, buf, sizeof buf, &length) == 0);
        if (response.flags & F_TXN) break;
    }
    assert(response.code == 5 && response.tflags == 0x10 && response.data_size == 200 && response.offsets_size == 8);
    assert(length >= response.br_size + 208);
    assert(!memcmp(buf + response.br_size, parcel, 164)); /* full token and service name */
    uint32_t translated_type, translated_flags, translated_handle, stability, isolated, priority;
    uint64_t delivered_offset;
    memcpy(&translated_type, buf + response.br_size + 164, 4);
    memcpy(&translated_flags, buf + response.br_size + 168, 4);
    memcpy(&translated_handle, buf + response.br_size + 172, 4);
    memcpy(&stability, buf + response.br_size + 188, 4);
    memcpy(&isolated, buf + response.br_size + 192, 4);
    memcpy(&priority, buf + response.br_size + 196, 4);
    memcpy(&delivered_offset, buf + response.br_size + 200, 8);
    assert(translated_type == 0x73682a85u && translated_flags == 0x113 && translated_handle != 0);
    assert(stability == registration.transaction.stability && isolated == 0 && priority == 8);
    assert(delivered_offset == candidate && candidate == 164);
    const uint32_t exception_none = 0;
    assert(rme_bproto_reply(&manager, &exception_none, sizeof exception_none) == 0);
    for (int i = 0; ; ++i) {
        assert(i < 4 && rme_bproto_receive(&caller, &response, buf, sizeof buf, &length) == 0);
        if (response.flags & F_TXN) break;
        assert(rme_bproto_read_request(&caller, 4096) == 0);
    }
    assert(response.data_size == sizeof exception_none);
    assert(!memcmp(buf + response.br_size, &exception_none, sizeof exception_none));
    rme_bproto_close(&manager); rme_bproto_close(&caller);
    puts("VIBRATION_BPROTO_DAEMON_OK");
    return 0;
}
