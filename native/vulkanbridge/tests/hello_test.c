#define _POSIX_C_SOURCE 200809L
#include "../protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Independent wire encoding: magic bytes RVK1; all fields are u32 LE. */
enum { MAGIC = 0x314b5652u, HELLO = 1, REPLY = 2, VERSION = 1 };
static int failures;

/* Exercise the real socket with positive short writes only in the selected
 * thread. Other test traffic and the peer use the ordinary send behavior. */
static _Thread_local int force_short_send;
static _Thread_local unsigned short_send_count;
static _Thread_local unsigned poll_interrupts_left, send_interrupts_left, recv_interrupts_left;
static _Thread_local unsigned poll_interrupts_seen, send_interrupts_seen, recv_interrupts_seen;
static _Thread_local long poll_interrupt_delay_ms;
static _Thread_local unsigned delay_ready_poll_at, ready_poll_calls;
int __real_poll(struct pollfd *fds, nfds_t count, int timeout);
int __wrap_poll(struct pollfd *fds, nfds_t count, int timeout) {
    if (poll_interrupts_left) {
        --poll_interrupts_left; ++poll_interrupts_seen;
        if (poll_interrupt_delay_ms) {
            struct timespec t = {poll_interrupt_delay_ms / 1000, (poll_interrupt_delay_ms % 1000) * 1000000};
            while (nanosleep(&t, &t) < 0 && errno == EINTR) {}
        }
        errno = EINTR;
        return -1;
    }
    int result = __real_poll(fds, count, timeout);
    if (delay_ready_poll_at && ++ready_poll_calls == delay_ready_poll_at && result > 0) {
        struct timespec t = {0, 160000000};
        while (nanosleep(&t, &t) < 0 && errno == EINTR) {}
    }
    return result;
}
ssize_t __real_send(int fd, const void *bytes, size_t length, int flags);
ssize_t __wrap_send(int fd, const void *bytes, size_t length, int flags) {
    if (send_interrupts_left) {
        --send_interrupts_left; ++send_interrupts_seen; errno = EINTR; return -1;
    }
    if (force_short_send && length > 2) {
        ++short_send_count;
        length = 2;
    }
    return __real_send(fd, bytes, length, flags);
}
ssize_t __real_recv(int fd, void *bytes, size_t length, int flags);
ssize_t __wrap_recv(int fd, void *bytes, size_t length, int flags) {
    if (recv_interrupts_left) {
        --recv_interrupts_left; ++recv_interrupts_seen; errno = EINTR; return -1;
    }
    return __real_recv(fd, bytes, length, flags);
}

static void inject_interrupts(unsigned n, long delay_ms) {
    poll_interrupts_left = send_interrupts_left = recv_interrupts_left = n;
    poll_interrupts_seen = send_interrupts_seen = recv_interrupts_seen = 0;
    poll_interrupt_delay_ms = delay_ms;
}

static void clear_interrupts(void) {
    poll_interrupts_left = send_interrupts_left = recv_interrupts_left = 0;
    poll_interrupt_delay_ms = 0;
}

static void check(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

static void le32(unsigned char *p, uint32_t x) {
    p[0] = (unsigned char)x; p[1] = (unsigned char)(x >> 8);
    p[2] = (unsigned char)(x >> 16); p[3] = (unsigned char)(x >> 24);
}

static void header(unsigned char *p, uint32_t magic, uint32_t type, uint32_t length) {
    le32(p, magic); le32(p + 4, type); le32(p + 8, length);
}

static int send_all(int fd, const void *bytes, size_t length) {
    const unsigned char *p = bytes;
    while (length) {
        ssize_t n = send(fd, p, length, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n; length -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *bytes, size_t length) {
    unsigned char *p = bytes;
    while (length) {
        ssize_t n = recv(fd, p, length, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        p += n; length -= (size_t)n;
    }
    return 0;
}

static void pause_ms(long ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000};
    while (nanosleep(&t, &t) < 0 && errno == EINTR) {}
}

static long elapsed_ms(struct timespec a, struct timespec b) {
    return (long)(b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
}

static int new_pair(int p[2]) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, p) == 0) return 1;
    perror("socketpair"); ++failures; return 0;
}

struct hello_args { int fd; uint32_t version; int timeout; int result; };
static void *server_worker(void *arg) {
    struct hello_args *a = arg;
    a->result = vkb_server_hello(a->fd, a->version, a->timeout);
    return NULL;
}

static void pair_case(uint32_t client, uint32_t server, int expected, const char *label) {
    int p[2]; pthread_t thread;
    if (!new_pair(p)) return;
    struct hello_args a = { p[1], server, 1500, -99 };
    if (pthread_create(&thread, NULL, server_worker, &a)) { check(0, "create server thread"); close(p[0]); close(p[1]); return; }
    int actual = vkb_client_hello(p[0], client, 1500);
    pthread_join(thread, NULL);
    check(actual == expected && a.result == expected, label);
    check(fcntl(p[0], F_GETFD) >= 0 && fcntl(p[1], F_GETFD) >= 0, "handshake leaves caller-owned descriptors open");
    close(p[0]); close(p[1]);
}

static void nonblocking_case(void) {
    int p[2]; pthread_t thread;
    if (!new_pair(p)) return;
    int f0 = fcntl(p[0], F_GETFL), f1 = fcntl(p[1], F_GETFL);
    if (f0 < 0 || f1 < 0 || fcntl(p[0], F_SETFL, f0 | O_NONBLOCK) < 0 ||
        fcntl(p[1], F_SETFL, f1 | O_NONBLOCK) < 0) {
        check(0, "set nonblocking sockets"); close(p[0]); close(p[1]); return;
    }
    struct hello_args a = {p[1], VERSION, 1500, -99};
    if (pthread_create(&thread, NULL, server_worker, &a)) {
        check(0, "create nonblocking server"); close(p[0]); close(p[1]); return;
    }
    int result = vkb_client_hello(p[0], VERSION, 1500);
    pthread_join(thread, NULL);
    check(result == 0 && a.result == 0, "nonblocking descriptors handshake");
    check((fcntl(p[0], F_GETFL) & O_NONBLOCK) && (fcntl(p[1], F_GETFL) & O_NONBLOCK),
          "handshake preserves nonblocking flag");
    close(p[0]); close(p[1]);
}

struct short_server_args { int fd, result; unsigned writes; };
static void *short_server_worker(void *arg) {
    struct short_server_args *a = arg;
    short_send_count = 0;
    force_short_send = 1;
    a->result = vkb_server_hello(a->fd, VERSION, 1500);
    force_short_send = 0;
    a->writes = short_send_count;
    return NULL;
}

static void short_send_case(int client) {
    int p[2]; pthread_t thread;
    if (!new_pair(p)) return;
    struct hello_args normal = {p[1], VERSION, 1500, -99};
    struct short_server_args shortened = {p[1], -99, 0};
    int create_result = client ? pthread_create(&thread, NULL, server_worker, &normal)
                               : pthread_create(&thread, NULL, short_server_worker, &shortened);
    if (create_result) { check(0, "create short-write peer"); close(p[0]); close(p[1]); return; }
    short_send_count = 0;
    force_short_send = client;
    int result = vkb_client_hello(p[0], VERSION, 1500);
    force_short_send = 0;
    unsigned writes = short_send_count;
    pthread_join(thread, NULL);
    if (client)
        check(result == 0 && normal.result == 0 && writes >= 2, "client completes after positive short sends");
    else
        check(result == 0 && shortened.result == 0 && shortened.writes >= 2, "server completes after positive short sends");
    close(p[0]); close(p[1]);
}

struct interrupted_server_args { int fd, result; unsigned polls, sends, recvs; };
static void *interrupted_server_worker(void *arg) {
    struct interrupted_server_args *a = arg;
    inject_interrupts(3, 0);
    a->result = vkb_server_hello(a->fd, VERSION, 1500);
    a->polls = poll_interrupts_seen; a->sends = send_interrupts_seen; a->recvs = recv_interrupts_seen;
    clear_interrupts();
    return NULL;
}

static void interrupted_case(void) {
    int p[2]; pthread_t thread;
    if (!new_pair(p)) return;
    struct interrupted_server_args a = {p[1], -99, 0, 0, 0};
    if (pthread_create(&thread, NULL, interrupted_server_worker, &a)) {
        check(0, "create interrupted server"); close(p[0]); close(p[1]); return;
    }
    inject_interrupts(3, 0);
    int result = vkb_client_hello(p[0], VERSION, 1500);
    unsigned polls = poll_interrupts_seen, sends = send_interrupts_seen, recvs = recv_interrupts_seen;
    clear_interrupts();
    pthread_join(thread, NULL);
    check(result == 0 && a.result == 0, "client and server retry interrupted poll/send/recv");
    check(polls == 3 && sends == 3 && recvs == 3 && a.polls == 3 && a.sends == 3 && a.recvs == 3,
          "interruptions reached poll/send/recv on both sides");
    close(p[0]); close(p[1]);
}

static void interrupted_deadline_case(void) {
    int p[2]; struct timespec before, after;
    if (!new_pair(p)) return;
    inject_interrupts(10, 40);
    /* Only poll is reached: the peer sends nothing. */
    clock_gettime(CLOCK_MONOTONIC, &before);
    int result = vkb_server_hello(p[0], VERSION, 110);
    clock_gettime(CLOCK_MONOTONIC, &after);
    unsigned interruptions = poll_interrupts_seen;
    clear_interrupts();
    check(result == -1 && interruptions > 0 && interruptions < 10 && elapsed_ms(before, after) < 1000,
          "repeated EINTR does not restart the overall deadline");
    close(p[0]); close(p[1]);
}

static void delayed_ready_deadline_case(void) {
    int p[2]; unsigned char frame[16], reply;
    if (!new_pair(p)) return;
    header(frame, MAGIC, HELLO, 4); le32(frame + 12, VERSION);
    check(send_all(p[1], frame, sizeof(frame)) == 0, "send request for delayed readiness");
    ready_poll_calls = 0;
    delay_ready_poll_at = 3; /* Delay delivery of the response-write readiness. */
    errno = 0;
    int result = vkb_server_hello(p[0], VERSION, 100);
    int result_errno = errno;
    delay_ready_poll_at = 0;
    check(result == -1 && result_errno == ETIMEDOUT,
          "readiness delivered after deadline does not report success");
    check(recv(p[1], &reply, 1, MSG_DONTWAIT) == -1 && (errno == EAGAIN || errno == EWOULDBLOCK),
          "response is not sent after delayed readiness exceeds deadline");
    close(p[0]); close(p[1]);
}

struct fragmented_args { int fd; int client_peer; int result; };
static void *fragmented_peer(void *arg) {
    struct fragmented_args *a = arg;
    unsigned char request[16], reply[20], observed[20];
    header(request, MAGIC, HELLO, 4); le32(request + 12, VERSION);
    header(reply, MAGIC, REPLY, 8); le32(reply + 12, VERSION); le32(reply + 16, 0);
    const unsigned char *out = a->client_peer ? reply : request;
    size_t out_len = a->client_peer ? sizeof(reply) : sizeof(request);
    if (a->client_peer && (recv_all(a->fd, observed, sizeof(request)) || memcmp(observed, request, sizeof(request)))) return NULL;
    for (size_t i = 0; i < out_len; ++i) {
        if (send_all(a->fd, out + i, 1)) return NULL;
        pause_ms(2);
    }
    if (!a->client_peer && (recv_all(a->fd, observed, sizeof(reply)) || memcmp(observed, reply, sizeof(reply)))) return NULL;
    a->result = 1;
    return NULL;
}

static void fragmented_case(int client) {
    int p[2]; pthread_t thread;
    if (!new_pair(p)) return;
    struct fragmented_args a = {p[1], client, 0};
    if (pthread_create(&thread, NULL, fragmented_peer, &a)) { check(0, "create fragmented peer"); close(p[0]); close(p[1]); return; }
    int result = client ? vkb_client_hello(p[0], VERSION, 2000) : vkb_server_hello(p[0], VERSION, 2000);
    if (result != 0) shutdown(p[0], SHUT_RDWR);
    pthread_join(thread, NULL);
    check(result == 0 && a.result, client ? "client reads fragmented response" : "server reads fragmented request");
    close(p[0]); close(p[1]);
}

static void malformed_server(uint32_t magic, uint32_t type, uint32_t size, uint32_t remote, const char *label) {
    int p[2]; unsigned char frame[16];
    if (!new_pair(p)) return;
    header(frame, magic, type, size); le32(frame + 12, remote);
    if (send_all(p[1], frame, sizeof(frame))) check(0, "write malformed request");
    check(vkb_server_hello(p[0], VERSION, 300) == -2, label);
    close(p[0]); close(p[1]);
}

static void malformed_client(uint32_t magic, uint32_t type, uint32_t size, uint32_t remote, uint32_t status, int expected, const char *label) {
    int p[2]; unsigned char frame[20];
    if (!new_pair(p)) return;
    header(frame, magic, type, size); le32(frame + 12, remote); le32(frame + 16, status);
    if (send_all(p[1], frame, sizeof(frame))) check(0, "write malformed response");
    check(vkb_client_hello(p[0], VERSION, 300) == expected, label);
    close(p[0]); close(p[1]);
}

static void truncated_case(int client, size_t size, int close_write, const char *label) {
    int p[2]; unsigned char frame[20]; struct timespec before, after;
    if (!new_pair(p)) return;
    header(frame, MAGIC, client ? REPLY : HELLO, client ? 8 : 4);
    le32(frame + 12, VERSION); le32(frame + 16, 0);
    if (send_all(p[1], frame, size)) check(0, "write partial frame");
    if (close_write) shutdown(p[1], SHUT_WR);
    clock_gettime(CLOCK_MONOTONIC, &before);
    int result = client ? vkb_client_hello(p[0], VERSION, 120) : vkb_server_hello(p[0], VERSION, 120);
    clock_gettime(CLOCK_MONOTONIC, &after);
    check(result == -1, label);
    if (!close_write) check(elapsed_ms(before, after) >= 70 && elapsed_ms(before, after) < 1200, "partial frame timeout is bounded");
    close(p[0]); close(p[1]);
}

struct drip_args { int fd; };
static void *drip_peer(void *arg) {
    struct drip_args *a = arg; unsigned char frame[16];
    header(frame, MAGIC, HELLO, 4); le32(frame + 12, VERSION);
    send_all(a->fd, frame, 5); pause_ms(70);
    send_all(a->fd, frame + 5, 7); pause_ms(70);
    send_all(a->fd, frame + 12, 4);
    return NULL;
}

static void total_deadline_case(void) {
    int p[2]; pthread_t thread; struct timespec before, after;
    if (!new_pair(p)) return;
    struct drip_args a = {p[1]};
    if (pthread_create(&thread, NULL, drip_peer, &a)) { check(0, "create drip peer"); close(p[0]); close(p[1]); return; }
    clock_gettime(CLOCK_MONOTONIC, &before);
    int result = vkb_server_hello(p[0], VERSION, 110);
    clock_gettime(CLOCK_MONOTONIC, &after);
    pthread_join(thread, NULL);
    check(result == -1 && elapsed_ms(before, after) >= 70 && elapsed_ms(before, after) < 1200,
          "deadline covers entire frame, not each read separately");
    close(p[0]); close(p[1]);
}

static int fill_send_buffer(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return 0;
    unsigned char bytes[4096] = {0};
    for (;;) {
        ssize_t n = send(fd, bytes, sizeof(bytes), MSG_NOSIGNAL);
        if (n > 0) continue;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        fcntl(fd, F_SETFL, flags); return 0;
    }
    return fcntl(fd, F_SETFL, flags) == 0;
}

static void send_timeout_case(int client) {
    int p[2]; unsigned char frame[16]; struct timespec before, after;
    if (!new_pair(p)) return;
    check(fill_send_buffer(p[0]), "fill send buffer");
    if (!client) {
        header(frame, MAGIC, HELLO, 4); le32(frame + 12, VERSION);
        check(send_all(p[1], frame, sizeof(frame)) == 0, "send request before blocked response");
    }
    clock_gettime(CLOCK_MONOTONIC, &before);
    int result = client ? vkb_client_hello(p[0], VERSION, 120) : vkb_server_hello(p[0], VERSION, 120);
    clock_gettime(CLOCK_MONOTONIC, &after);
    check(result == -1 && elapsed_ms(before, after) >= 70 && elapsed_ms(before, after) < 1200,
          client ? "client send obeys timeout" : "server send obeys timeout");
    close(p[0]); close(p[1]);
}

static void no_overread_case(int client) {
    int p[2]; unsigned char frame[24], tail[4] = {0xde, 0xad, 0xbe, 0xef}, seen[4];
    if (!new_pair(p)) return;
    size_t n = client ? 20 : 16;
    header(frame, MAGIC, client ? REPLY : HELLO, client ? 8 : 4);
    le32(frame + 12, VERSION); if (client) le32(frame + 16, 0);
    memcpy(frame + n, tail, sizeof(tail));
    check(send_all(p[1], frame, n + sizeof(tail)) == 0, "write frame and following bytes");
    int result = client ? vkb_client_hello(p[0], VERSION, 300) : vkb_server_hello(p[0], VERSION, 300);
    check(result == 0 && recv(p[0], seen, sizeof(seen), MSG_PEEK | MSG_DONTWAIT) == (ssize_t)sizeof(seen) && !memcmp(seen, tail, sizeof(tail)),
          client ? "client consumes exactly one response" : "server consumes exactly one request");
    close(p[0]); close(p[1]);
}

static void closed_peer_cases(void) {
    int p[2]; unsigned char frame[16];
    if (!new_pair(p)) return;
    close(p[1]);
    check(vkb_client_hello(p[0], VERSION, 300) == -1, "client handles closed peer without SIGPIPE");
    close(p[0]);
    if (!new_pair(p)) return;
    header(frame, MAGIC, HELLO, 4); le32(frame + 12, VERSION);
    check(send_all(p[1], frame, sizeof(frame)) == 0, "send request before peer closes");
    close(p[1]);
    check(vkb_server_hello(p[0], VERSION, 300) == -1, "server handles closed peer without SIGPIPE");
    close(p[0]);
}

int main(void) {
    signal(SIGPIPE, SIG_DFL);
    check(vkb_client_hello(-1, VERSION, 0) == -2, "client rejects zero timeout");
    check(vkb_server_hello(-1, VERSION, 60001) == -2, "server rejects oversized timeout");
    check(vkb_client_hello(-1, 0, 100) == -2 && vkb_server_hello(-1, 0, 100) == -2, "zero version is invalid");
    check(vkb_client_hello(-1, VERSION, 100) == -1 && vkb_server_hello(-1, VERSION, 100) == -1, "invalid descriptor is I/O failure");
    errno = 0;
    check(vkb_client_hello(-1, VERSION, 100) == -1 && errno == EBADF,
          "client reports invalid descriptor instead of timeout");
    errno = 0;
    check(vkb_server_hello(-1, VERSION, 100) == -1 && errno == EBADF,
          "server reports invalid descriptor instead of timeout");
    pair_case(VERSION, VERSION, 0, "matching protocol versions accepted");
    pair_case(VERSION, VERSION + 1, 1, "different protocol versions rejected by both sides");
    nonblocking_case(); short_send_case(1); short_send_case(0); interrupted_case(); interrupted_deadline_case(); delayed_ready_deadline_case();
    fragmented_case(0); fragmented_case(1);
    malformed_server(0, HELLO, 4, VERSION, "server rejects wrong magic");
    malformed_server(MAGIC, 123, 4, VERSION, "server rejects unknown type");
    malformed_server(MAGIC, HELLO, 0, VERSION, "server rejects short length");
    malformed_server(MAGIC, HELLO, UINT32_MAX, VERSION, "server rejects oversized length immediately");
    malformed_server(MAGIC, HELLO, 4, 0, "server rejects zero requested version");
    malformed_client(0, REPLY, 8, VERSION, 0, -2, "client rejects wrong magic");
    malformed_client(MAGIC, 123, 8, VERSION, 0, -2, "client rejects unknown type");
    malformed_client(MAGIC, REPLY, 4, VERSION, 0, -2, "client rejects short length");
    malformed_client(MAGIC, REPLY, UINT32_MAX, VERSION, 0, -2, "client rejects oversized length immediately");
    malformed_client(MAGIC, REPLY, 8, VERSION, 7, -2, "client rejects invalid status");
    malformed_client(MAGIC, REPLY, 8, 0, 0, -2, "client rejects zero server version");
    malformed_client(MAGIC, REPLY, 8, VERSION + 1, 0, -2, "client rejects success for different version");
    malformed_client(MAGIC, REPLY, 8, VERSION, 1, 1, "same-version rejection is never treated as success");
    truncated_case(0, 6, 1, "server reports truncated header EOF");
    truncated_case(0, 14, 1, "server reports truncated payload EOF");
    truncated_case(1, 6, 1, "client reports truncated header EOF");
    truncated_case(1, 16, 1, "client reports truncated payload EOF");
    truncated_case(0, 6, 0, "server times out on partial header");
    truncated_case(0, 14, 0, "server times out on partial payload");
    truncated_case(1, 6, 0, "client times out on partial header");
    truncated_case(1, 16, 0, "client times out on partial payload");
    total_deadline_case(); send_timeout_case(0); send_timeout_case(1);
    no_overread_case(0); no_overread_case(1); closed_peer_cases();
    if (failures) { fprintf(stderr, "HELLO_TEST_FAILURES=%d\n", failures); return 1; }
    puts("VULKAN_BRIDGE_HELLO_OK");
    return 0;
}
