/* Real socketpair protocol, fake OpenCL loader; no hardware access. */
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int opencl_discovery_serve(int fd);
int opencl_discovery_roundtrip(char *result, size_t size);

enum { MAGIC = 0x31444352, MAX_PAYLOAD = 4096 };
struct session { int fd[2]; pthread_t thread; };
struct response { int32_t status; uint32_t size; unsigned char data[MAX_PAYLOAD]; };
static pthread_t main_thread;
static int injection, phase;

ssize_t __real_recv(int fd, void *buf, size_t len, int flags);
void *__real_dlopen(const char *path, int flags);
void *__wrap_dlopen(const char *path, int flags) {
    if (injection == 7 && path && !strcmp(path, "libOpenCL.so")) return NULL;
    return __real_dlopen(path, flags);
}

static void put32(unsigned char *b, uint32_t n);

ssize_t __wrap_recv(int fd, void *buf, size_t len, int flags) {
    if (injection >= 1 && injection <= 4 && pthread_equal(main_thread, pthread_self())) {
        unsigned char header[12] = {0};
        put32(header, MAGIC);
        put32(header + 4, injection == 4 ? (uint32_t)-1 : 0);
        put32(header + 8, injection == 1 ? MAX_PAYLOAD + 1u : 4);
        if (phase++ == 0 && len >= sizeof(header)) {
            size_t n = injection == 2 ? 4 : sizeof(header);
            memcpy(buf, header, n);
            return (ssize_t)n;
        }
        if (injection == 3 && phase == 2 && len >= 2) {
            memset(buf, 0, 2);
            return 2;
        }
        return 0;
    }
    return __real_recv(fd, buf, len, flags);
}

static void *serve(void *arg) {
    return (void *)(intptr_t)opencl_discovery_serve(*(int *)arg);
}

static int start(struct session *s) {
    return socketpair(AF_UNIX, SOCK_STREAM, 0, s->fd) ||
           pthread_create(&s->thread, NULL, serve, &s->fd[1]);
}

static int finish(struct session *s) {
    void *status = NULL;
    shutdown(s->fd[0], SHUT_WR);
    int error = pthread_join(s->thread, &status);
    close(s->fd[0]);
    close(s->fd[1]);
    return error ? -1 : (int)(intptr_t)status;
}

static int transfer(int fd, void *buf, size_t size, int writing) {
    unsigned char *p = buf;
    while (size) {
        ssize_t n = writing ? write(fd, p, size) : read(fd, p, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 1;
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static void put32(unsigned char *b, uint32_t n) {
    for (int i = 0; i < 4; ++i) b[i] = (unsigned char)(n >> (8 * i));
}

static uint32_t get32(const unsigned char *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static int request(struct session *s, uint32_t op, uint32_t id, uint32_t param,
                   struct response *response) {
    unsigned char wire[16], header[12];
    put32(wire, MAGIC); put32(wire + 4, op); put32(wire + 8, id); put32(wire + 12, param);
    if (transfer(s->fd[0], wire, sizeof(wire), 1) ||
        transfer(s->fd[0], header, sizeof(header), 0) || get32(header) != MAGIC) return 1;
    response->status = (int32_t)get32(header + 4);
    response->size = get32(header + 8);
    return response->size > MAX_PAYLOAD ||
           transfer(s->fd[0], response->data, response->size, 0);
}

static int is_string(struct response *r, const char *expected) {
    size_t n = strlen(expected) + 1;
    return r->status == 0 && r->size == n && memcmp(r->data, expected, n) == 0;
}

static int enum_platform(struct session *s, uint32_t *id) {
    struct response r;
    if (request(s, 1, 0, 0, &r) || r.status || r.size != 4) return 1;
    *id = get32(r.data);
    return *id == 0;
}

static int full(void) {
    struct session s;
    struct response r;
    uint32_t platform, device;
    void *fake = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
    if (!fake) return 1;
    uintptr_t (*platform_pointer)(void) = dlsym(fake, "fake_platform_pointer");
    uintptr_t (*device_pointer)(void) = dlsym(fake, "fake_device_pointer");
    if (!platform_pointer || !device_pointer || start(&s) || enum_platform(&s, &platform) ||
        (uintptr_t)platform == platform_pointer() || platform == (uint32_t)platform_pointer()) return 1;
    if (request(&s, 2, platform, 0x0902, &r) || !is_string(&r, "Fake OpenCL Platform")) return 1;
    if (request(&s, 3, platform, 0, &r) || r.status || r.size != 4) return 1;
    device = get32(r.data);
    if (!device || (uintptr_t)device == device_pointer() || device == (uint32_t)device_pointer()) return 1;
    if (request(&s, 4, device, 0x102b, &r) || !is_string(&r, "Fake GPU")) return 1;
    if (request(&s, 4, device, 0x102f, &r) || !is_string(&r, "OpenCL 1.2 fake")) return 1;
    if (request(&s, 5, device, 0x1000, &r) || r.status || r.size != 8 ||
        get32(r.data) != 4 || get32(r.data + 4) != 0) return 1;
    if (finish(&s)) return 1;
    dlclose(fake);
    puts("all discovery requests OK");
    return 0;
}

static int cross(void) {
    struct session a, b;
    struct response r;
    uint32_t old_id, new_id;
    if (start(&a) || enum_platform(&a, &old_id) || finish(&a)) return 1;
    if (start(&b) || enum_platform(&b, &new_id) || old_id == new_id ||
        request(&b, 2, old_id, 0x0902, &r) || r.status == 0 || r.size != 0 || finish(&b)) return 1;
    puts("cross-session ID rejected");
    return 0;
}

static int missing_object(const char *mode) {
    struct session s;
    struct response r;
    uint32_t platform;
    if (start(&s)) return 1;
    if (!strcmp(mode, "no_platforms")) {
        if (request(&s, 1, 0, 0, &r) || r.status != -1001 || r.size != 0) return 1;
    } else {
        if (enum_platform(&s, &platform) || request(&s, 3, platform, 0, &r) ||
            r.status != -1 || r.size != 0) return 1;
    }
    return finish(&s) != 0;
}

static int disconnected(void) {
    struct session s;
    unsigned char req[16] = {0};
    if (start(&s)) return 1;
    put32(req, MAGIC); put32(req + 4, 1);
    if (shutdown(s.fd[0], SHUT_RD) || transfer(s.fd[0], req, sizeof(req), 1)) return 1;
    shutdown(s.fd[0], SHUT_WR);
    void *status = NULL;
    if (pthread_join(s.thread, &status)) return 1;
    close(s.fd[0]); close(s.fd[1]);
    return (intptr_t)status == 0;
}

static int inconsistent_length(int device_query) {
    struct session s;
    struct response r;
    uint32_t platform, device;
    if (start(&s) || enum_platform(&s, &platform)) return 1;
    if (device_query) {
        if (request(&s, 3, platform, 0, &r) || r.status || r.size != 4) return 1;
        device = get32(r.data);
        if (request(&s, 4, device, 0x102b, &r)) return 1;
    } else if (request(&s, 2, platform, 0x0902, &r)) return 1;
    if (!r.status || r.size) return 1;
    return finish(&s) != 0;
}

static int inconsistent_scalar(uint32_t param) {
    struct session s;
    struct response r;
    uint32_t platform, device;
    if (start(&s) || enum_platform(&s, &platform) ||
        request(&s, 3, platform, 0, &r) || r.status || r.size != 4) return 1;
    device = get32(r.data);
    if (request(&s, 5, device, param, &r) || !r.status || r.size) return 1;
    return finish(&s) != 0;
}

static int malformed(const char *mode) {
    struct session s;
    unsigned char req[16] = {0};
    if (start(&s)) return 1;
    if (!strcmp(mode, "eof")) return finish(&s) != 0;
    put32(req, !strcmp(mode, "badmagic") ? 0x12345678u : MAGIC);
    put32(req + 4, !strcmp(mode, "badop") ? 99 : 1);
    size_t length = !strcmp(mode, "short") ? 7 : sizeof(req);
    if (transfer(s.fd[0], req, length, 1)) return 1;
    shutdown(s.fd[0], SHUT_WR);
    void *status = NULL;
    if (pthread_join(s.thread, &status)) return 1;
    unsigned char header[12];
    ssize_t n = recv(s.fd[0], header, sizeof(header), MSG_DONTWAIT);
    close(s.fd[0]); close(s.fd[1]);
    return (intptr_t)status == 0 && !(n == 12 && get32(header) == MAGIC && (int32_t)get32(header + 4) != 0);
}

int main(int argc, char **argv) {
    main_thread = pthread_self();
    if (argc != 2) return 1;
    if (!strcmp(argv[1], "full")) return full();
    if (!strcmp(argv[1], "cross")) return cross();
    if (!strcmp(argv[1], "no_platforms") || !strcmp(argv[1], "no_devices")) return missing_object(argv[1]);
    if (!strcmp(argv[1], "disconnected")) return disconnected();
    if (!strcmp(argv[1], "platform_length")) return inconsistent_length(0);
    if (!strcmp(argv[1], "device_length")) return inconsistent_length(1);
    if (!strcmp(argv[1], "type_length")) return inconsistent_scalar(0x1000);
    if (!strcmp(argv[1], "compiler_length")) return inconsistent_scalar(0x1028);
    if (!strcmp(argv[1], "roundtrip_small")) {
        char result[1] = {0};
        return opencl_discovery_roundtrip(result, sizeof(result)) == 0;
    }
    if (!strcmp(argv[1], "roundtrip") || !strncmp(argv[1], "inject", 6)) {
        if (!strcmp(argv[1], "inject_oversize")) injection = 1;
        if (!strcmp(argv[1], "inject_short_header")) injection = 2;
        if (!strcmp(argv[1], "inject_short_payload")) injection = 3;
        if (!strcmp(argv[1], "inject_error_payload")) injection = 4;
        if (!strcmp(argv[1], "inject_no_library")) injection = 7;
        char result[1024] = {0};
        int status = opencl_discovery_roundtrip(result, sizeof(result));
        puts(result);
        return status ? 1 : 0;
    }
    return malformed(argv[1]);
}
