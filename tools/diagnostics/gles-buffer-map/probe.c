/* GLES 3 regression: two objects mapped concurrently through the same target. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { BYTES = 64 };
static int failures;

static int gl_ok(const char *where) {
    GLenum error = glGetError();
    if (error == GL_NO_ERROR) return 1;
    fprintf(stderr, "FAIL %s: GL error 0x%x\n", where, error);
    ++failures;
    return 0;
}

/* Each case has its own context so an unexpected mapping does not contaminate
 * another case. Only use valid GL objects; never dereference a failed map. */
static int case_multi_target(void) {
    int failed = 0;
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, BYTES, NULL, GL_DYNAMIC_DRAW);
    void *first = glMapBufferRange(GL_ARRAY_BUFFER, 0, BYTES, GL_MAP_WRITE_BIT);
    if (!gl_ok("multi-target setup") || !first) { fprintf(stderr, "FAIL multi-target initial map\n"); failed = 1; goto done; }
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
    void *reported = NULL;
    glGetBufferPointerv(GL_COPY_WRITE_BUFFER, GL_BUFFER_MAP_POINTER, &reported);
    if (!gl_ok("multi-target pointer") || reported != first) {
        fprintf(stderr, "FAIL same object on another target has wrong pointer\n"); failed = 1;
    }
    void *second = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, BYTES, GL_MAP_WRITE_BIT);
    GLenum error = glGetError();
    printf("same object second target: %s error=0x%x\n", second ? "non-null" : "NULL", error);
    if (second || error != GL_INVALID_OPERATION) {
        fprintf(stderr, "FAIL duplicate map should return NULL and INVALID_OPERATION\n"); failed = 1;
    }
    /* Deletion and context destruction perform cleanup even for a broken
     * implementation that incorrectly returned a second pointer. */
done:
    if (buffer) glDeleteBuffers(1, &buffer);
    if (!gl_ok("multi-target cleanup")) failed = 1;
    return failed;
}

/* Unity uses COPY_WRITE_BUFFER with explicit flush and unsynchronized writes. */
static int case_unity_copy_write(void) {
    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT |
        GL_MAP_FLUSH_EXPLICIT_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
    enum { OFFSET = 8, LENGTH = 32 };
    GLuint buffer[2] = {0, 0};
    void *mapped[2] = {NULL, NULL};
    int failed = 0;
    glGenBuffers(2, buffer);
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer[i]);
        glBufferData(GL_COPY_WRITE_BUFFER, BYTES, NULL, GL_DYNAMIC_DRAW);
    }
    if (!gl_ok("Unity copy-write allocation") || !buffer[0] || !buffer[1]) {
        fprintf(stderr, "FAIL Unity copy-write allocation\n"); failed = 1; goto done;
    }
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer[i]);
        mapped[i] = glMapBufferRange(GL_COPY_WRITE_BUFFER, OFFSET, LENGTH, flags);
        GLenum error = glGetError();
        printf("Unity copy-write map %c access=0x%x: %s error=0x%x\n", 'A' + i,
            flags, mapped[i] ? "non-null" : "NULL", error);
        if (!mapped[i] || error != GL_NO_ERROR) {
            fprintf(stderr, "FAIL Unity copy-write map %c\n", 'A' + i); failed = 1; goto done;
        }
        memset(mapped[i], i ? 0x77 : 0x33, LENGTH);
    }
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer[i]);
        glFlushMappedBufferRange(GL_COPY_WRITE_BUFFER, 0, LENGTH);
        if (!gl_ok("Unity explicit flush")) { failed = 1; goto done; }
        GLboolean ok = glUnmapBuffer(GL_COPY_WRITE_BUFFER);
        if (!gl_ok("Unity unmap") || ok != GL_TRUE) {
            fprintf(stderr, "FAIL Unity copy-write unmap %c\n", 'A' + i); failed = 1; goto done;
        }
        mapped[i] = NULL;
    }
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer[i]);
        uint8_t *readback = glMapBufferRange(GL_COPY_WRITE_BUFFER, OFFSET, LENGTH, GL_MAP_READ_BIT);
        if (!gl_ok("Unity read map") || !readback) {
            fprintf(stderr, "FAIL Unity copy-write read map %c\n", 'A' + i); failed = 1; goto done;
        }
        mapped[i] = readback;
        for (int j = 0; j < LENGTH; ++j) if (readback[j] != (i ? 0x77 : 0x33)) {
            fprintf(stderr, "FAIL Unity copy-write readback %c byte %d: 0x%02x\n", 'A' + i, j, readback[j]); failed = 1; break;
        }
        GLboolean ok = glUnmapBuffer(GL_COPY_WRITE_BUFFER);
        if (!gl_ok("Unity read unmap") || ok != GL_TRUE) { fprintf(stderr, "FAIL Unity read unmap %c\n", 'A' + i); failed = 1; }
        mapped[i] = NULL;
    }
done:
    glDeleteBuffers(2, buffer);
    if (!gl_ok("Unity copy-write cleanup")) failed = 1;
    return failed;
}

static int case_delete_mapped(void) {
    int failed = 0;
    GLuint buffer[2] = {0, 0};
    glGenBuffers(2, buffer);
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, buffer[i]);
        glBufferData(GL_ARRAY_BUFFER, BYTES, NULL, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, buffer[0]);
    void *first = glMapBufferRange(GL_ARRAY_BUFFER, 0, BYTES, GL_MAP_WRITE_BIT);
    if (!gl_ok("delete setup") || !first) { fprintf(stderr, "FAIL delete initial map\n"); failed = 1; goto done; }
    glDeleteBuffers(1, &buffer[0]);
    if (!gl_ok("delete mapped buffer")) { failed = 1; goto done; }
    GLuint old_name = buffer[0];
    buffer[0] = 0;
    glBindBuffer(GL_ARRAY_BUFFER, buffer[1]);
    void *second = glMapBufferRange(GL_ARRAY_BUFFER, 0, BYTES, GL_MAP_WRITE_BIT);
    GLenum error = glGetError();
    printf("map other object after deletion: %s error=0x%x\n", second ? "non-null" : "NULL", error);
    if (!second || error != GL_NO_ERROR) { fprintf(stderr, "FAIL deletion retained stale map\n"); failed = 1; }
    else if (glUnmapBuffer(GL_ARRAY_BUFFER) != GL_TRUE || !gl_ok("unmap after deletion")) { failed = 1; }

    /* Name reuse is implementation-dependent; never turn absence into PASS. */
    GLuint names[256] = {0};
    glGenBuffers(256, names);
    if (!gl_ok("generate reuse candidates")) { failed = 1; goto done; }
    int found = 0;
    for (int i = 0; i < 256; ++i) if (names[i] == old_name) found = 1;
    if (!found) printf("SKIP deleted name reuse: implementation did not reuse name\n");
    else {
        glBindBuffer(GL_ARRAY_BUFFER, old_name);
        glBufferData(GL_ARRAY_BUFFER, BYTES, NULL, GL_DYNAMIC_DRAW);
        void *p = glMapBufferRange(GL_ARRAY_BUFFER, 0, BYTES, GL_MAP_WRITE_BIT);
        error = glGetError();
        printf("reused name map: %s error=0x%x\n", p ? "non-null" : "NULL", error);
        if (!p || error != GL_NO_ERROR) { fprintf(stderr, "FAIL reused name inherited stale mapping\n"); failed = 1; }
        else if (glUnmapBuffer(GL_ARRAY_BUFFER) != GL_TRUE || !gl_ok("unmap reused name")) failed = 1;
    }
    glDeleteBuffers(256, names);
    if (!gl_ok("reuse cleanup")) failed = 1;
done:
    glDeleteBuffers(2, buffer);
    if (!gl_ok("delete cleanup")) failed = 1;
    return failed;
}

static void run_isolated(EGLDisplay display, EGLConfig config, EGLSurface surface,
                         EGLContext original, const char *name, int (*test)(void)) {
    const EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext isolated = eglCreateContext(display, config, EGL_NO_CONTEXT, attrs);
    if (isolated == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, isolated)) {
        fprintf(stderr, "FAIL %s context: 0x%x\n", name, eglGetError()); ++failures;
    } else {
        int failed = test();
        printf("%s: %s\n", name, failed ? "FAIL" : "PASS");
        failures += failed;
    }
    if (!eglMakeCurrent(display, surface, surface, original)) { fprintf(stderr, "FAIL restore original context\n"); ++failures; }
    if (isolated != EGL_NO_CONTEXT && !eglDestroyContext(display, isolated)) { fprintf(stderr, "FAIL destroy isolated context\n"); ++failures; }
}

static void run_shared(EGLDisplay display, EGLConfig config, EGLSurface surface, EGLContext original) {
    const EGLint attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext one = eglCreateContext(display, config, EGL_NO_CONTEXT, attrs);
    EGLContext two = one != EGL_NO_CONTEXT ? eglCreateContext(display, config, one, attrs) : EGL_NO_CONTEXT;
    GLuint buffer = 0;
    int failed = 0;
    if (one == EGL_NO_CONTEXT || two == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, one)) {
        fprintf(stderr, "FAIL shared context setup: 0x%x\n", eglGetError()); failed = 1; goto done;
    }
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, BYTES, NULL, GL_DYNAMIC_DRAW);
    void *first = glMapBufferRange(GL_ARRAY_BUFFER, 0, BYTES, GL_MAP_WRITE_BIT);
    if (!gl_ok("shared initial map") || !first) { fprintf(stderr, "FAIL shared initial map\n"); failed = 1; goto done; }
    if (!eglMakeCurrent(display, surface, surface, two)) { fprintf(stderr, "FAIL switch to shared context\n"); failed = 1; goto done; }
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    void *reported = NULL;
    glGetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, &reported);
    if (!gl_ok("shared pointer") || reported != first) { fprintf(stderr, "FAIL shared context does not see same map\n"); failed = 1; }
    void *second = glMapBufferRange(GL_ARRAY_BUFFER, 0, BYTES, GL_MAP_WRITE_BIT);
    GLenum error = glGetError();
    printf("shared duplicate map: %s error=0x%x\n", second ? "non-null" : "NULL", error);
    if (second || error != GL_INVALID_OPERATION) { fprintf(stderr, "FAIL duplicate map across shared contexts\n"); failed = 1; }
    if (!eglDestroyContext(display, one)) { fprintf(stderr, "FAIL destroy first shared context\n"); failed = 1; }
    else one = EGL_NO_CONTEXT;
    reported = NULL;
    glGetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, &reported);
    if (!gl_ok("shared pointer after context destroy") || reported != first) {
        fprintf(stderr, "FAIL mapping lost after first context destroy\n"); failed = 1;
    }
done:
    /* Destroying the final context owns any remaining mapped resources. */
    if (!eglMakeCurrent(display, surface, surface, original)) { fprintf(stderr, "FAIL restore after shared contexts\n"); failed = 1; }
    if (one != EGL_NO_CONTEXT && !eglDestroyContext(display, one)) { fprintf(stderr, "FAIL destroy first context\n"); failed = 1; }
    if (two != EGL_NO_CONTEXT && !eglDestroyContext(display, two)) { fprintf(stderr, "FAIL destroy second context\n"); failed = 1; }
    printf("shared context: %s\n", failed ? "FAIL" : "PASS");
    failures += failed;
}

int main(void) {
    setbuf(stdout, NULL);
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config;
    EGLint count = 0, major = 0, minor = 0;
    int initialized = 0, current = 0;
    GLuint buffer[2] = {0, 0};
    void *mapped[2] = {NULL, NULL};
    const EGLint config_attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    const EGLint surface_attrs[] = {EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
    const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};

    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
        fprintf(stderr, "FAIL EGL initialize: 0x%x\n", eglGetError()); ++failures; goto done;
    }
    initialized = 1;
    if (!eglChooseConfig(display, config_attrs, &config, 1, &count) || count != 1) {
        fprintf(stderr, "FAIL EGL config: 0x%x count=%d\n", eglGetError(), count); ++failures; goto done;
    }
    surface = eglCreatePbufferSurface(display, config, surface_attrs);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "FAIL EGL context: 0x%x\n", eglGetError()); ++failures; goto done;
    }
    current = 1;
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *version = (const char *)glGetString(GL_VERSION);
    printf("EGL=%d.%d renderer=%s version=%s\n", major, minor,
        renderer ? renderer : "(null)", version ? version : "(null)");
    if (!renderer || !version || !gl_ok("renderer/version")) { ++failures; goto done; }
    GLint gl_major = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
    if (!gl_ok("major version") || gl_major < 3) {
        fprintf(stderr, "FAIL requires GLES 3, got %d\n", gl_major); ++failures; goto done;
    }

    run_isolated(display, config, surface, context, "same buffer / multiple targets", case_multi_target);
    run_isolated(display, config, surface, context, "delete mapped buffer", case_delete_mapped);
    run_shared(display, config, surface, context);
    run_isolated(display, config, surface, context, "Unity COPY_WRITE explicit flush", case_unity_copy_write);

    glGenBuffers(2, buffer);
    if (!gl_ok("generate buffers") || !buffer[0] || !buffer[1] || buffer[0] == buffer[1]) {
        fprintf(stderr, "FAIL could not create two distinct buffer objects\n"); ++failures; goto done;
    }
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, buffer[i]);
        glBufferData(GL_ARRAY_BUFFER, BYTES, NULL, GL_DYNAMIC_DRAW);
        if (!gl_ok("allocate buffer")) goto done;
    }
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, buffer[i]);
        mapped[i] = glMapBufferRange(GL_ARRAY_BUFFER, 0, BYTES,
            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        GLenum error = glGetError();
        printf("map %c: %s error=0x%x\n", 'A' + i, mapped[i] ? "non-null" : "NULL", error);
        if (!mapped[i] || error != GL_NO_ERROR) {
            fprintf(stderr, "FAIL mapping %c while earlier buffer remains mapped\n", 'A' + i);
            ++failures; goto done;
        }
        memset(mapped[i], i ? 0x5a : 0xa5, BYTES);
    }
    for (int i = 0; i < 2; ++i) {
        void *reported = NULL;
        glBindBuffer(GL_ARRAY_BUFFER, buffer[i]);
        glGetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER, &reported);
        if (!gl_ok("get mapped pointer") || reported != mapped[i]) {
            fprintf(stderr, "FAIL buffer %c returned wrong mapped pointer\n", 'A' + i); ++failures; goto done;
        }
        GLboolean ok = glUnmapBuffer(GL_ARRAY_BUFFER);
        if (!gl_ok("unmap buffer") || ok != GL_TRUE) {
            fprintf(stderr, "FAIL unmap %c\n", 'A' + i); ++failures; goto done;
        }
        mapped[i] = NULL;
    }
    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, buffer[i]);
        uint8_t *readback = glMapBufferRange(GL_ARRAY_BUFFER, 0, BYTES, GL_MAP_READ_BIT);
        if (!gl_ok("read map") || !readback) {
            fprintf(stderr, "FAIL read map %c\n", 'A' + i); ++failures; goto done;
        }
        mapped[i] = readback;
        for (int j = 0; j < BYTES; ++j) if (readback[j] != (i ? 0x5a : 0xa5)) {
            fprintf(stderr, "FAIL readback %c byte %d: 0x%02x\n", 'A' + i, j, readback[j]); ++failures; break;
        }
        GLboolean ok = glUnmapBuffer(GL_ARRAY_BUFFER);
        if (!gl_ok("unmap readback") || ok != GL_TRUE) { fprintf(stderr, "FAIL read unmap %c\n", 'A' + i); ++failures; }
        mapped[i] = NULL;
    }
done:
    if (current) {
        for (int i = 0; i < 2; ++i) if (mapped[i]) {
            glBindBuffer(GL_ARRAY_BUFFER, buffer[i]);
            if (glUnmapBuffer(GL_ARRAY_BUFFER) != GL_TRUE || !gl_ok("cleanup unmap")) {
                fprintf(stderr, "FAIL cleanup unmap %c\n", 'A' + i); ++failures;
            }
        }
        if (buffer[0] || buffer[1]) glDeleteBuffers(2, buffer);
        if (!gl_ok("cleanup buffers")) ++failures;
        if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) { fprintf(stderr, "FAIL release EGL context\n"); ++failures; }
    }
    if (context != EGL_NO_CONTEXT && !eglDestroyContext(display, context)) { fprintf(stderr, "FAIL destroy EGL context\n"); ++failures; }
    if (surface != EGL_NO_SURFACE && !eglDestroySurface(display, surface)) { fprintf(stderr, "FAIL destroy EGL surface\n"); ++failures; }
    if (initialized && !eglTerminate(display)) { fprintf(stderr, "FAIL terminate EGL\n"); ++failures; }
    printf("diagnostic failures=%d\n", failures);
    return failures ? 1 : 0;
}
