/* Internal debug discovery over an already-connected local stream; not a listener. */
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef void *cl_platform_id;
typedef void *cl_device_id;
typedef int32_t cl_int;
typedef uint32_t cl_uint;
typedef uint64_t cl_device_type;

enum { MAGIC = 0x31444352, MAX_PLATFORMS = 32, MAX_DEVICES = 128,
       MAX_REPLY = 4096, ERR_LOADER = -32001, ERR_SYMBOL = -32002,
       ERR_LIMIT = -32003, ERR_TRANSPORT = -32004, ERR_PROTOCOL = -32005,
       CL_INVALID_VALUE = -30, CL_INVALID_PLATFORM = -32, CL_INVALID_DEVICE = -33,
       CL_DEVICE_NOT_FOUND = -1, CL_PLATFORM_NOT_FOUND = -1001 };
#define CL_DEVICE_TYPE_ALL UINT64_C(0xffffffff)

struct platform { cl_platform_id handle; uint32_t id; };
struct device { cl_device_id handle; uint32_t id, platform; };
struct session {
    void *library;
    cl_int (*get_platforms)(cl_uint, cl_platform_id *, cl_uint *);
    cl_int (*get_platform_info)(cl_platform_id, cl_uint, size_t, void *, size_t *);
    cl_int (*get_devices)(cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);
    cl_int (*get_device_info)(cl_device_id, cl_uint, size_t, void *, size_t *);
    struct platform platforms[MAX_PLATFORMS];
    struct device devices[MAX_DEVICES];
    unsigned np, nd;
    int platforms_loaded;
    int device_loaded[MAX_PLATFORMS];
    int device_error[MAX_PLATFORMS];
    int init_error;
};

static pthread_mutex_t ids_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t next_id = 1;
static uint32_t allocate_id(void) {
    uint32_t id;
    pthread_mutex_lock(&ids_lock);
    id = next_id;
    if (next_id) ++next_id; /* never reuse an exhausted ID */
    pthread_mutex_unlock(&ids_lock);
    return id;
}

static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(unsigned char *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (8 * i));
}
/* 1 complete, 0 clean EOF before a frame, -1 truncated or transport error. */
static int read_exact(int fd, void *buffer, size_t size) {
    unsigned char *p = buffer;
    size_t done = 0;
    while (done < size) {
        ssize_t n = recv(fd, p + done, size - done, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return n == 0 && !done ? 0 : -1;
        done += (size_t)n;
    }
    return 1;
}
static int write_exact(int fd, const void *buffer, size_t size) {
    const unsigned char *p = buffer;
    size_t done = 0;
    while (done < size) {
        ssize_t n = send(fd, p + done, size - done, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        done += (size_t)n;
    }
    return 0;
}

static void session_init(struct session *s) {
    memset(s, 0, sizeof(*s));
    s->library = dlopen("libOpenCL.so", RTLD_NOW | RTLD_LOCAL);
    if (!s->library) { s->init_error = ERR_LOADER; return; }
    s->get_platforms = dlsym(s->library, "clGetPlatformIDs");
    s->get_platform_info = dlsym(s->library, "clGetPlatformInfo");
    s->get_devices = dlsym(s->library, "clGetDeviceIDs");
    s->get_device_info = dlsym(s->library, "clGetDeviceInfo");
    if (!s->get_platforms || !s->get_platform_info || !s->get_devices || !s->get_device_info)
        s->init_error = ERR_SYMBOL;
}
static int load_platforms(struct session *s) {
    cl_platform_id handles[MAX_PLATFORMS];
    cl_uint count = 0;
    int err;
    if (s->platforms_loaded) return 0;
    err = s->get_platforms(0, NULL, &count);
    if (err) return err;
    if (!count) return CL_PLATFORM_NOT_FOUND;
    if (count > MAX_PLATFORMS) return ERR_LIMIT;
    err = s->get_platforms(count, handles, NULL);
    if (err) return err;
    for (unsigned i = 0; i < count; ++i) {
        uint32_t id = allocate_id();
        if (!id) return ERR_LIMIT;
        s->platforms[i] = (struct platform){handles[i], id};
    }
    s->np = count;
    s->platforms_loaded = 1;
    return 0;
}
static int find_platform(struct session *s, uint32_t id) {
    for (unsigned i = 0; i < s->np; ++i) if (s->platforms[i].id == id) return (int)i;
    return -1;
}
static int find_device(struct session *s, uint32_t id) {
    for (unsigned i = 0; i < s->nd; ++i) if (s->devices[i].id == id) return (int)i;
    return -1;
}
static int load_devices(struct session *s, unsigned platform) {
    cl_device_id handles[MAX_DEVICES];
    cl_uint count = 0;
    int err;
    if (s->device_loaded[platform]) return s->device_error[platform];
    err = s->get_devices(s->platforms[platform].handle, CL_DEVICE_TYPE_ALL, 0, NULL, &count);
    if (!err && !count) err = CL_DEVICE_NOT_FOUND;
    if (!err && (count > MAX_DEVICES || count > MAX_DEVICES - s->nd)) err = ERR_LIMIT;
    if (!err) err = s->get_devices(s->platforms[platform].handle, CL_DEVICE_TYPE_ALL, count, handles, NULL);
    if (!err) for (unsigned i = 0; i < count; ++i) {
        uint32_t id = allocate_id();
        if (!id) { err = ERR_LIMIT; break; }
        s->devices[s->nd++] = (struct device){handles[i], id, platform};
    }
    s->device_loaded[platform] = 1;
    return s->device_error[platform] = err;
}
static int dispatch(struct session *s, uint32_t op, uint32_t id, uint32_t param,
                    unsigned char *out, uint32_t *length) {
    int i, err;
    size_t n = 0, actual = 0;
    *length = 0;
    if (s->init_error) return s->init_error;
    if (op == 1) {
        if (id || param) return CL_INVALID_VALUE;
        if ((err = load_platforms(s))) return err;
        for (unsigned j = 0; j < s->np; ++j) put32(out + 4 * j, s->platforms[j].id);
        *length = 4 * s->np;
        return 0;
    }
    if (op == 2 || op == 3) {
        i = find_platform(s, id);
        if (i < 0) return CL_INVALID_PLATFORM;
        if (op == 3) {
            if (param) return CL_INVALID_VALUE;
            if ((err = load_devices(s, (unsigned)i))) return err;
            for (unsigned j = 0; j < s->nd; ++j) if (s->devices[j].platform == (unsigned)i) {
                put32(out + *length, s->devices[j].id); *length += 4;
            }
            return 0;
        }
        if (param != 0x0901 && param != 0x0902 && param != 0x0903) return CL_INVALID_VALUE;
        err = s->get_platform_info(s->platforms[i].handle, param, 0, NULL, &n);
        if (err) return err;
        if (!n || n > MAX_REPLY) return ERR_LIMIT;
        memset(out, 0, n);
        err = s->get_platform_info(s->platforms[i].handle, param, n, out, &actual);
    } else if (op == 4 || op == 5) {
        i = find_device(s, id);
        if (i < 0) return CL_INVALID_DEVICE;
        if (op == 5) {
            if (param == 0x1000) {
                uint64_t value = 0;
                err = s->get_device_info(s->devices[i].handle, param, sizeof(value), &value, &actual);
                if (err) return err;
                if (actual != sizeof(value)) return ERR_PROTOCOL;
                put32(out, (uint32_t)value); put32(out + 4, (uint32_t)(value >> 32)); *length = 8;
                return 0;
            }
            if (param == 0x1028) {
                uint32_t value = 0;
                err = s->get_device_info(s->devices[i].handle, param, sizeof(value), &value, &actual);
                if (err) return err;
                if (actual != sizeof(value)) return ERR_PROTOCOL;
                put32(out, value); *length = 4;
                return 0;
            }
            return CL_INVALID_VALUE;
        }
        if (param != 0x102b && param != 0x102c && param != 0x102f) return CL_INVALID_VALUE;
        err = s->get_device_info(s->devices[i].handle, param, 0, NULL, &n);
        if (err) return err;
        if (!n || n > MAX_REPLY) return ERR_LIMIT;
        memset(out, 0, n);
        err = s->get_device_info(s->devices[i].handle, param, n, out, &actual);
    } else return CL_INVALID_VALUE;
    if (err) return err;
    if (actual != n || out[n - 1] != 0) return ERR_PROTOCOL;
    *length = (uint32_t)n;
    return 0;
}

/* The caller owns fd; no pathname, listener or network endpoint is created. */
int opencl_discovery_serve(int fd) {
    struct session s;
    unsigned char request[16], response[12], payload[MAX_REPLY];
    int result = -1;
    session_init(&s);
    for (;;) {
        int r = read_exact(fd, request, sizeof(request));
        if (!r) { result = 0; break; }
        if (r < 0 || get32(request) != MAGIC) break;
        uint32_t length = 0;
        int status = dispatch(&s, get32(request + 4), get32(request + 8), get32(request + 12), payload, &length);
        put32(response, MAGIC); put32(response + 4, (uint32_t)status); put32(response + 8, length);
        if (write_exact(fd, response, sizeof(response)) || (length && write_exact(fd, payload, length))) break;
    }
    if (s.library) dlclose(s.library);
    return result;
}

static int request(int fd, uint32_t op, uint32_t id, uint32_t param,
                   unsigned char *payload, size_t capacity, uint32_t *length) {
    unsigned char req[16], header[12];
    put32(req, MAGIC); put32(req + 4, op); put32(req + 8, id); put32(req + 12, param);
    if (write_exact(fd, req, sizeof(req)) || read_exact(fd, header, sizeof(header)) != 1) return ERR_TRANSPORT;
    if (get32(header) != MAGIC) return ERR_PROTOCOL;
    int32_t status = (int32_t)get32(header + 4);
    *length = get32(header + 8);
    if (*length > capacity || (status && *length)) return ERR_PROTOCOL;
    if (*length && read_exact(fd, payload, *length) != 1) return ERR_TRANSPORT;
    return status;
}
static void *server_thread(void *opaque) {
    int fd = *(int *)opaque;
    (void)opencl_discovery_serve(fd);
    shutdown(fd, SHUT_RDWR);
    return NULL;
}

/* Debug entry point: one in-process socketpair, no exported command or listener. */
int opencl_discovery_roundtrip(char *result, size_t size) {
    int fd[2] = {-1, -1}, status = ERR_TRANSPORT, running = 0;
    pthread_t worker;
    unsigned char payload[MAX_REPLY];
    uint32_t platforms[MAX_PLATFORMS], devices[MAX_DEVICES], count, length;
    uint32_t device = 0, selected_platform = 0;
    char platform_name[256], device_name[256], device_version[256];
    uint64_t type = 0;
    if (!result || !size) return 1;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fd) ||
        pthread_create(&worker, NULL, server_thread, &fd[1])) goto cleanup;
    running = 1;
    status = request(fd[0], 1, 0, 0, payload, sizeof(payload), &length);
    if (status) goto cleanup;
    if (!length || length % 4 || length / 4 > MAX_PLATFORMS) { status = ERR_PROTOCOL; goto cleanup; }
    count = length / 4;
    for (uint32_t i = 0; i < count; ++i) platforms[i] = get32(payload + 4 * i);
    for (uint32_t i = 0; i < count; ++i) {
        status = request(fd[0], 3, platforms[i], 0, payload, sizeof(payload), &length);
        if (status == CL_DEVICE_NOT_FOUND) continue;
        if (status) goto cleanup;
        if (!length || length % 4 || length / 4 > MAX_DEVICES) { status = ERR_PROTOCOL; goto cleanup; }
        uint32_t device_count = length / 4;
        for (uint32_t j = 0; j < device_count; ++j) devices[j] = get32(payload + 4 * j);
        for (uint32_t j = 0; j < device_count; ++j) {
            status = request(fd[0], 5, devices[j], 0x1000, payload, sizeof(payload), &length);
            if (status) goto cleanup;
            if (length != 8) { status = ERR_PROTOCOL; goto cleanup; }
            uint64_t candidate_type = (uint64_t)get32(payload) | ((uint64_t)get32(payload + 4) << 32);
            if (!device || (candidate_type & 4)) {
                device = devices[j]; selected_platform = platforms[i]; type = candidate_type;
            }
            if (candidate_type & 4) goto selected;
        }
    }
selected:
    if (!device) { status = CL_DEVICE_NOT_FOUND; goto cleanup; }
    status = request(fd[0], 2, selected_platform, 0x0902, payload, sizeof(payload), &length);
    if (status) goto cleanup;
    if (!length || payload[length - 1]) { status = ERR_PROTOCOL; goto cleanup; }
    snprintf(platform_name, sizeof(platform_name), "%.*s", (int)(length - 1), (char *)payload);
    status = request(fd[0], 4, device, 0x102b, payload, sizeof(payload), &length);
    if (status) goto cleanup;
    if (!length || payload[length - 1]) { status = ERR_PROTOCOL; goto cleanup; }
    snprintf(device_name, sizeof(device_name), "%.*s", (int)(length - 1), (char *)payload);
    status = request(fd[0], 4, device, 0x102f, payload, sizeof(payload), &length);
    if (status) goto cleanup;
    if (!length || payload[length - 1]) { status = ERR_PROTOCOL; goto cleanup; }
    snprintf(device_version, sizeof(device_version), "%.*s", (int)(length - 1), (char *)payload);
    for (char *p = platform_name; *p; ++p) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) *p = '?';
    for (char *p = device_name; *p; ++p) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) *p = '?';
    for (char *p = device_version; *p; ++p) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) *p = '?';
    int printed = snprintf(result, size,
                           "OpenCL socket discovery: platform=%.100s%s device=%.100s%s version=%.100s%s type=0x%llx",
                           platform_name, strlen(platform_name) > 100 ? " (truncated)" : "",
                           device_name, strlen(device_name) > 100 ? " (truncated)" : "",
                           device_version, strlen(device_version) > 100 ? " (truncated)" : "",
                           (unsigned long long)type);
    if (printed < 0 || (size_t)printed >= size) status = ERR_LIMIT;
cleanup:
    if (fd[0] >= 0) { shutdown(fd[0], SHUT_RDWR); close(fd[0]); }
    if (running) pthread_join(worker, NULL);
    if (fd[1] >= 0) close(fd[1]);
    if (status) {
        const char *reason = status == ERR_LOADER ? "library unavailable" :
                             status == ERR_SYMBOL ? "required symbol missing" :
                             status == ERR_LIMIT ? "size or resource limit" :
                             status == ERR_TRANSPORT ? "transport error" :
                             status == ERR_PROTOCOL ? "protocol error" : "OpenCL error";
        snprintf(result, size, "OpenCL socket discovery failed: %s (%d)", reason, status);
    }
    return status ? 1 : 0;
}
