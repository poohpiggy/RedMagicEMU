#define _GNU_SOURCE
#include "../vibration_bproto_client.h"
#include "../bproto.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    int s[2];
    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, s) == 0);
    struct rme_bproto_client c;
    assert(rme_bproto_open(&c, s[0], getpid(), getuid(), 1000, 42) == 0);
    struct c_hello hello;
    assert(recv(s[1], &hello, sizeof hello, 0) == sizeof hello);
    assert(hello.h.op == OP_HELLO && hello.h.euid == 1000);
    assert(hello.pid == (unsigned)getpid() && hello.main == 1 && hello.pad == 42);

    unsigned char data[] = {1, 2, 3, 4};
    uint64_t offset = 0;
    /* Offsets must describe full Binder objects; malformed offsets fail closed. */
    assert(rme_bproto_txn(&c, 0, 1, 0, data, sizeof data, &offset, 1) < 0);
    assert(rme_bproto_txn(&c, 0, 1, 0, data, sizeof data, NULL, 0) == 0);
    unsigned char packet[256];
    ssize_t n = recv(s[1], packet, sizeof packet, 0);
    assert(n == (ssize_t)(sizeof(struct c_txn) + sizeof data));
    struct c_txn m; memcpy(&m, packet, sizeof m);
    assert(m.h.op == OP_TXN && m.h.euid == 1000 && m.code == 1 && m.data_size == 4);
    assert(!memcmp(packet + sizeof m, data, sizeof data));
    unsigned char object[24] = {0};
    uint32_t object_type = 0x66642a85u; /* Binder FD objects are never sent by this client. */
    memcpy(object, &object_type, sizeof object_type);
    assert(rme_bproto_txn(&c, 0, 1, 0, object, sizeof object, &offset, 1) < 0);
    object_type = 0x73622a85u; /* T_BINDER */
    memcpy(object, &object_type, sizeof object_type);
    assert(rme_bproto_txn(&c, 0, 1, 0, object, sizeof object, &offset, 1) == 0);
    n = recv(s[1], packet, sizeof packet, 0);
    assert(n == (ssize_t)(sizeof(struct c_txn) + sizeof object + sizeof offset));
    memcpy(&m, packet, sizeof m);
    assert(m.offsets_size == sizeof offset && m.data_size == sizeof object);
    assert(!memcmp(packet + sizeof m, object, sizeof object));
    assert(!memcmp(packet + sizeof m + sizeof object, &offset, sizeof offset));
    assert(rme_bproto_reply(&c, data, sizeof data) == 0);
    n = recv(s[1], packet, sizeof packet, 0);
    assert(n == (ssize_t)(sizeof(struct c_txn) + sizeof data));
    memcpy(&m, packet, sizeof m);
    assert(m.cmd == 0x40406301u && m.data_size == sizeof data);

    assert(rme_bproto_read_request(&c, 4096) == 0);
    struct c_hdr read_header;
    assert(recv(s[1], &read_header, sizeof read_header, 0) == sizeof read_header);
    assert(read_header.op == OP_READ && read_header.flags == F_READ && read_header.read_size == 4096);

    struct d_hdr out; unsigned char payload[128]; size_t length = 0;
    struct d_hdr response = {0};
    assert(send(s[1], &response, sizeof response, 0) == sizeof response);
    assert(rme_bproto_receive(&c, &out, payload, sizeof payload, &length) == 0 && length == 0);

    /* An unexpected descriptor closes the client, never dispatching the message. */
    char controls[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec iov = { .iov_base = &response, .iov_len = sizeof response };
    struct msghdr message = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = controls, .msg_controllen = sizeof controls };
    struct cmsghdr *control = CMSG_FIRSTHDR(&message);
    control->cmsg_level = SOL_SOCKET; control->cmsg_type = SCM_RIGHTS; control->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(control), &s[1], sizeof(int));
    assert(sendmsg(s[1], &message, 0) == sizeof response);
    assert(rme_bproto_receive(&c, &out, payload, sizeof payload, &length) < 0);
    assert(c.fd == -1);

    rme_bproto_close(&c); rme_bproto_close(&c);
    assert(send(s[1], data, sizeof data, MSG_NOSIGNAL) < 0);
    close(s[1]);
    int short_s[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, short_s) == 0);
    assert(rme_bproto_open(&c, short_s[0], getpid(), getuid(), 1000, 44) == 0);
    assert(recv(short_s[1], &hello, sizeof hello, 0) == sizeof hello);
    assert(send(short_s[1], data, sizeof data, 0) == sizeof data);
    assert(rme_bproto_receive(&c, &out, payload, sizeof payload, &length) < 0 && c.fd == -1);
    close(short_s[1]);
    int malformed[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, malformed) == 0);
    assert(rme_bproto_open(&c, malformed[0], getpid(), getuid(), 1000, 45) == 0);
    assert(recv(malformed[1], &hello, sizeof hello, 0) == sizeof hello);
    struct d_hdr malformed_header = { .flags = F_TXN, .data_size = 24, .offsets_size = 8 };
    unsigned char malformed_packet[sizeof malformed_header + 32] = {0};
    memcpy(malformed_packet, &malformed_header, sizeof malformed_header);
    object_type = 0x66642a85u;
    memcpy(malformed_packet + sizeof malformed_header, &object_type, sizeof object_type);
    assert(send(malformed[1], malformed_packet, sizeof malformed_packet, 0) == sizeof malformed_packet);
    assert(rme_bproto_receive(&c, &out, payload, sizeof payload, &length) < 0 && c.fd == -1);
    close(malformed[1]);
    int other[2]; assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, other) == 0);
    assert(rme_bproto_open(&c, other[0], getpid() + 1, getuid(), 1000, 43) < 0);
    assert(fcntl(other[0], F_GETFD) < 0 && errno == EBADF);
    close(other[1]);
    puts("VIBRATION_BPROTO_CLIENT_OK");
    return 0;
}
