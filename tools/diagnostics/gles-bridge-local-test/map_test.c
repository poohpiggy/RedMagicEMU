/* Exercise the actual GLES bridge encoder in-process with a memory-only host.
 * No Android, Guest, host GL driver or socket is used. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "../../../native/glbridge/guest.c"

enum { FAKE_FD = 314159, CAPACITY = 131072 };
enum { ARRAY_BUFFER = 0x8892, COPY_READ = 0x8f36, COPY_WRITE = 0x8f37 };
enum { MAP_POINTER = 0x88bd, WRITE_INVALIDATE_EXPLICIT_UNSYNC = 0x36 };
static struct ts stream;
static uint32_t next_context = 101;
static struct ectx *created_contexts[4];
static size_t created_count;
static uint8_t sent[CAPACITY], replies[CAPACITY];
static size_t sent_size, reply_read, reply_size;
static uint32_t host_context, host_error, next_buffer_name = 3;
static struct { uint32_t context, target, name; } host_bindings[32];

static uint32_t word(const uint8_t *p) { uint32_t v; memcpy(&v, p, sizeof(v)); return v; }
static uint64_t qword(const uint8_t *p) { uint64_t v; memcpy(&v, p, sizeof(v)); return v; }
static void fake_reply(const void *data, size_t size) {
    if (size > sizeof(replies) - reply_size) abort();
    memcpy(replies + reply_size, data, size);
    reply_size += size;
}
static void fake_reply32(uint32_t value) { fake_reply(&value, sizeof(value)); }
static uint32_t *host_binding(uint32_t context_id, uint32_t target) {
    for (size_t i = 0; i < sizeof(host_bindings)/sizeof(host_bindings[0]); ++i) {
        if (host_bindings[i].context == context_id && host_bindings[i].target == target)
            return &host_bindings[i].name;
        if (!host_bindings[i].context) {
            host_bindings[i].context = context_id;
            host_bindings[i].target = target;
            return &host_bindings[i].name;
        }
    }
    abort();
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void)prio; (void)tag; (void)fmt; return 0;
}
int __system_property_get(const char *name, char *value) {
    if (!strcmp(name, "debug.aemu.gl")) { value[0] = '3'; value[1] = 0; return 1; }
    value[0] = 0; return 0;
}
ssize_t __real_write(int fd, const void *data, size_t len);
ssize_t __wrap_write(int fd, const void *data, size_t len) {
    if (fd == FAKE_FD) {
        const uint8_t *bytes = data;
        if (len > sizeof(sent) - sent_size) { errno = ENOSPC; return -1; }
        memcpy(sent + sent_size, bytes, len); sent_size += len;
        for (size_t i = 0; i < len;) {
            if (len - i < 8) { errno = EIO; return -1; }
            uint32_t op = word(bytes+i), n = word(bytes+i+4); i += 8;
            if (n > len - i) { errno = EIO; return -1; }
            const uint8_t *p = bytes+i;
            if (op == OP_CTX_CREATE) fake_reply32(next_context++);
            else if (op == IDX_glBindBuffer && n == 8)
                *host_binding(host_context, word(p)) = word(p+4);
            else if (op == IDX_glGenBuffers && n == 4) {
                uint32_t count = word(p);
                if (count > 16) { errno = EIO; return -1; }
                while (count--) fake_reply32(next_buffer_name++);
            } else if (op == IDX_glDeleteBuffers && n >= 8) {
                uint32_t count = word(p), bytes_count = word(p+4);
                if (count > 16 || bytes_count != count*4 || n < 8+bytes_count) { errno = EIO; return -1; }
                for (uint32_t j = 0; j < count; ++j) {
                    uint32_t name = word(p+8+j*4);
                    for (size_t k = 0; k < sizeof(host_bindings)/sizeof(host_bindings[0]); ++k)
                        if (host_bindings[k].context == host_context && host_bindings[k].name == name)
                            host_bindings[k].name = 0;
                }
            } else if ((op == IDX_glGetBufferParameteriv || op == IDX_glGetBufferParameteri64v) && n == 8) {
                uint32_t target = word(p), pname = word(p+4);
                uint64_t value = pname == 0x8764 ? 64 : 0; /* GL_BUFFER_SIZE / actual host mapped state */
                if (!*host_binding(host_context, target)) host_error = 0x0502;
                fake_reply(&value, op == IDX_glGetBufferParameteri64v ? 8 : 4);
            } else if (op == IDX_glGetError && n == 0) {
                fake_reply32(host_error); host_error = 0;
            } else if (op == OP_MAP_READ && n == 16) {
                uint32_t size = word(p+12);
                if (size > 4096 || size > sizeof(replies)-reply_size) { errno = EIO; return -1; }
                memset(replies+reply_size, 0, size); reply_size += size;
            }
            i += n;
        }
        return (ssize_t)len;
    }
    return __real_write(fd, data, len);
}
ssize_t __real_read(int fd, void *data, size_t len);
ssize_t __wrap_read(int fd, void *data, size_t len) {
    if (fd != FAKE_FD) return __real_read(fd, data, len);
    if (len > reply_size-reply_read) { errno = EIO; return -1; }
    memcpy(data, replies+reply_read, len); reply_read += len;
    if (reply_read == reply_size) reply_read = reply_size = 0;
    return (ssize_t)len;
}

static int check(int condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
static int setup(void) {
    memset(&stream, 0, sizeof(stream));
    stream.fd = FAKE_FD;
    stream.cap = CAPACITY;
    stream.buf = calloc(CAPACITY, 1);
    if (!stream.buf) return 0;
    t_ts = &stream;
    return 1;
}
static struct ectx *context(struct ectx *share) {
    const EGLint attribs[] = { 0x3098, 3, 0x3038 };
    struct ectx *c = eglCreateContext(NULL, NULL, share, attribs);
    if (!c || !c->bridged) {
        fprintf(stderr, "FAIL: real bridge context creation\n");
        if (c) eglDestroyContext(NULL, c);
        return NULL;
    }
    if (c && created_count < sizeof(created_contexts) / sizeof(created_contexts[0]))
        created_contexts[created_count++] = c;
    return c;
}
static int destroy_context(struct ectx *c) {
    EGLBoolean ok = eglDestroyContext(NULL, c);
    if (ok) for (size_t i = 0; i < created_count; ++i)
        if (created_contexts[i] == c) created_contexts[i] = NULL;
    return ok;
}
static void teardown(void) {
    for (size_t i = created_count; i > 0; --i) {
        struct ectx *c = created_contexts[i - 1];
        if (c) { stream.ctx = c; destroy_context(c); }
    }
    stream.ctx = NULL;
    t_ts = NULL;
    free(stream.buf);
}
static void make_current_for_encoder(struct ectx *c) {
    if (stream.ctx != c) flush(&stream);
    stream.ctx = c;
    host_context = c ? c->host : 0;
}
static void *pointer(GLenum target) {
    void *p = (void *)(uintptr_t)1;
    e_glGetBufferPointerv(target, MAP_POINTER, &p);
    return p;
}
static void *map(GLenum target, long off, long len) {
    return e_glMapBufferRange(target, off, len, WRITE_INVALIDATE_EXPLICIT_UNSYNC);
}
static int buffers(int count) {
    GLuint names[2] = {0};
    e_glGenBuffers(count, names);
    for (int i = 0; i < count; ++i) {
        if (!check(names[i] == (GLuint)(3+i), "generated buffer name")) return 0;
        e_glBindBuffer(COPY_WRITE, names[i]);
        e_glBufferData(COPY_WRITE, 64, NULL, 0x88e8);
    }
    return 1;
}

/* Decode only the production encoder's bind and explicit write commands.
 * The fake host tracks a byte array per object name, not a GL implementation. */
static int check_writes(void) {
    uint8_t objects[8][32] = {{0}};
    uint8_t history[CAPACITY*2];
    if (sent_size > sizeof(history)-stream.n) return check(0, "command history too large");
    memcpy(history, sent, sent_size); memcpy(history+sent_size, stream.buf, stream.n);
    size_t history_size = sent_size + stream.n;
    uint32_t bound = 0;
    for (size_t i = 0; i < history_size;) {
        if (history_size - i < 8) return check(0, "truncated command header");
        uint32_t op = word(history + i), n = word(history + i + 4);
        i += 8;
        if (n > history_size - i) return check(0, "truncated command payload");
        const uint8_t *p = history + i;
        if (op == IDX_glBindBuffer && n == 8 && word(p) == COPY_WRITE) {
            bound = word(p + 4);
        } else if (op == OP_MAP_WRITE) {
            if (n < 16 || word(p) != COPY_WRITE || bound >= 8) return check(0, "invalid map write");
            uint64_t off = qword(p + 4);
            uint32_t len = word(p + 12);
            if (off > 32 || len > 32 - off || n != 16 + ((len + 3u) & ~3u))
                return check(0, "invalid map write range");
            memcpy(objects[bound] + off, p + 16, len);
        }
        i += n;
    }
    for (size_t i = 0; i < 32; ++i) {
        uint8_t expected_a = i >= 17 && i < 21 ? 0xa1 : 0;
        uint8_t expected_b = i >= 6 && i < 9 ? 0xb2 : 0;
        if (objects[3][i] != expected_a || objects[4][i] != expected_b)
            return check(0, "explicit flush reached wrong object, offset or length");
    }
    return 1;
}

static int same_target(void) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    if (!buffers(2)) return 0;
    e_glBindBuffer(COPY_WRITE, 3);
    void *a = map(COPY_WRITE, 0, 8);
    if (!check(a != NULL, "first buffer map")) return 0;
    e_glBindBuffer(COPY_WRITE, 4);
    void *b = map(COPY_WRITE, 0, 8);
    if (!check(b != NULL && b != a, "two buffers on one target map independently")) return 0;
    if (!check(pointer(COPY_WRITE) == b, "second buffer pointer")) return 0;
    e_glBindBuffer(COPY_WRITE, 3);
    if (!check(pointer(COPY_WRITE) == a, "first buffer pointer after rebinding")) return 0;
    return 1;
}
static int cross_target(void) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    if (!buffers(1)) return 0;
    e_glBindBuffer(COPY_WRITE, 3);
    void *a = map(COPY_WRITE, 0, 8);
    if (!check(a != NULL, "map buffer on first target")) return 0;
    e_glBindBuffer(COPY_READ, 3);
    if (!check(pointer(COPY_READ) == a, "same object exposes its map via another target")) return 0;
    if (!check(map(COPY_READ, 0, 8) == NULL, "same object cannot be mapped twice")) return 0;
    return 1;
}
static int shared_context(int destroy_first) {
    struct ectx *a = context(NULL); if (!a) return 0;
    struct ectx *b = context(a); if (!b) return 0;
    make_current_for_encoder(a);
    if (!buffers(1)) return 0;
    e_glBindBuffer(COPY_WRITE, 3);
    void *p = map(COPY_WRITE, 0, 8);
    if (!check(p != NULL, "map in first shared context")) return 0;
    make_current_for_encoder(b);
    e_glBindBuffer(COPY_WRITE, 3);
    if (destroy_first && !check(destroy_context(a), "destroy first context")) return 0;
    if (!check(pointer(COPY_WRITE) == p, "shared object map survives in remaining context")) return 0;
    if (!check(e_glUnmapBuffer(COPY_WRITE), "unmap shared buffer from another context")) return 0;
    return 1;
}
static int explicit_flush(void) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    if (!buffers(2)) return 0;
    e_glBindBuffer(COPY_WRITE, 3);
    uint8_t *a = map(COPY_WRITE, 16, 8);
    if (!check(a != NULL, "first explicit map")) return 0;
    memset(a, 0xa1, 8);
    e_glBindBuffer(COPY_WRITE, 4);
    uint8_t *b = map(COPY_WRITE, 4, 8);
    if (!check(b != NULL && b != a, "second explicit map")) return 0;
    memset(b, 0xb2, 8);
    e_glFlushMappedBufferRange(COPY_WRITE, 2, 3);
    if (!check(e_glUnmapBuffer(COPY_WRITE), "unmap second buffer")) return 0;
    e_glBindBuffer(COPY_WRITE, 3);
    e_glFlushMappedBufferRange(COPY_WRITE, 1, 4);
    if (!check(e_glUnmapBuffer(COPY_WRITE), "unmap first buffer")) return 0;
    return check_writes();
}
static int deleted_reuse(void) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    if (!buffers(1)) return 0;
    e_glBindBuffer(COPY_WRITE, 3);
    if (!check(map(COPY_WRITE, 0, 8) != NULL, "map before delete")) return 0;
    GLuint name = 3;
    e_glDeleteBuffers(1, &name);
    next_buffer_name = 3;
    e_glGenBuffers(1, &name);
    if (!check(name == 3, "host reused deleted name")) return 0;
    e_glBindBuffer(COPY_WRITE, 3);
    e_glBufferData(COPY_WRITE, 64, NULL, 0x88e8);
    if (!check(pointer(COPY_WRITE) == NULL, "reused name has no old mapped pointer")) return 0;
    if (!check(map(COPY_WRITE, 0, 8) != NULL, "reused name can be mapped")) return 0;
    return 1;
}
static int reused_name_shared(void) {
    struct ectx *a = context(NULL); if (!a) return 0;
    struct ectx *b = context(a); if (!b) return 0;
    make_current_for_encoder(a);
    if (!buffers(1)) return 0;
    make_current_for_encoder(b);
    GLuint name = 3;
    e_glDeleteBuffers(1, &name);
    next_buffer_name = 3;
    e_glGenBuffers(1, &name);
    if (!check(name == 3, "reused name generated")) return 0;
    e_glBindBuffer(COPY_WRITE, name);
    e_glBufferData(COPY_WRITE, 64, NULL, 0x88e8);
    void *new_map = map(COPY_WRITE, 0, 8);
    if (!check(new_map != NULL, "map new object")) return 0;
    make_current_for_encoder(a);
    if (!check(pointer(COPY_WRITE) == NULL, "old object must not expose new object's map")) return 0;
    void *old_map = map(COPY_WRITE, 0, 8);
    if (!check(old_map && old_map != new_map, "old and new objects map independently")) return 0;
    make_current_for_encoder(b);
    if (!check(pointer(COPY_WRITE) == new_map, "new object's pointer survives context switch")) return 0;
    e_glDeleteBuffers(1, &name);
    make_current_for_encoder(a);
    if (!check(pointer(COPY_WRITE) == old_map, "deleting new object preserves old object's map")) return 0;
    return check(e_glUnmapBuffer(COPY_WRITE), "unmap old object");
}
static int mapped_state(void) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    if (!buffers(1)) return 0;
    if (!check(map(COPY_WRITE, 8, 16) != NULL, "map for state queries")) return 0;
    GLint mapped = -1, flags = -1;
    GLint64 offset = -1, length = -1;
    e_glGetBufferParameteriv(COPY_WRITE, 0x88bc, &mapped);
    e_glGetBufferParameteriv(COPY_WRITE, 0x911f, &flags);
    e_glGetBufferParameteri64v(COPY_WRITE, 0x9121, &offset);
    e_glGetBufferParameteri64v(COPY_WRITE, 0x9120, &length);
    int ok = check(mapped == 1, "GL_BUFFER_MAPPED while mapped");
    ok &= check(flags == WRITE_INVALIDATE_EXPLICIT_UNSYNC, "GL_BUFFER_ACCESS_FLAGS");
    ok &= check(offset == 8, "GL_BUFFER_MAP_OFFSET");
    ok &= check(length == 16, "GL_BUFFER_MAP_LENGTH");
    ok &= check(e_glUnmapBuffer(COPY_WRITE), "unmap for state queries");
    e_glGetBufferParameteriv(COPY_WRITE, 0x88bc, &mapped);
    ok &= check(mapped == 0, "GL_BUFFER_MAPPED after unmap");
    return ok;
}
static int invalid_map(const char *name) {
    struct ectx *c = context(NULL); if (!c) return 0;
    make_current_for_encoder(c);
    long offset = 0, length = 8;
    GLbitfield access = WRITE_INVALIDATE_EXPLICIT_UNSYNC;
    GLenum expected = 0x0501;
    if (strcmp(name, "invalid-no-binding") && !buffers(1)) return 0;
    if (!strcmp(name, "invalid-no-binding")) expected = 0x0502;
    else if (!strcmp(name, "invalid-negative-offset")) offset = -1;
    else if (!strcmp(name, "invalid-negative-length")) length = -1;
    else if (!strcmp(name, "invalid-bounds")) { offset = 60; length = 8; }
    else if (!strcmp(name, "invalid-bits")) access |= 0x40000000;
    else if (!strcmp(name, "invalid-access")) { access = 4; expected = 0x0502; }
    else if (!strcmp(name, "invalid-read-unsync")) { access = 0x21; expected = 0x0502; }
    else return 0;
    void *p = e_glMapBufferRange(COPY_WRITE, offset, length, access);
    GLenum error = local_glGetError();
    int ok = check(p == NULL, "invalid map must return NULL");
    if (error != expected) fprintf(stderr, "FAIL: GL error expected 0x%x, got 0x%x\n", expected, error);
    return ok && error == expected;
}
int main(int argc, char **argv) {
    if (argc != 2 || !setup()) return 2;
    int ok;
    if (!strcmp(argv[1], "same-target")) ok = same_target();
    else if (!strcmp(argv[1], "cross-target")) ok = cross_target();
    else if (!strcmp(argv[1], "shared")) ok = shared_context(0);
    else if (!strcmp(argv[1], "destroy-shared")) ok = shared_context(1);
    else if (!strcmp(argv[1], "explicit-flush")) ok = explicit_flush();
    else if (!strcmp(argv[1], "delete-reuse")) ok = deleted_reuse();
    else if (!strcmp(argv[1], "reused-name-shared")) ok = reused_name_shared();
    else if (!strcmp(argv[1], "mapped-state")) ok = mapped_state();
    else if (!strncmp(argv[1], "invalid-", 8)) ok = invalid_map(argv[1]);
    else { fprintf(stderr, "unknown case: %s\n", argv[1]); return 2; }
    teardown();
    if (ok) puts("PRODUCTION_GLES_MAP_CASE_OK");
    return ok ? 0 : 1;
}
