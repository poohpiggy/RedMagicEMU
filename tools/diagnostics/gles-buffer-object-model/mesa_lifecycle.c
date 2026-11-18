/* Compare the limited object model with a native GLES driver's actual bindings. */
#include "model.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>

static void require(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}
static void gl_ok(const char *what) { GLenum e = glGetError(); if (e) { fprintf(stderr, "%s: 0x%x\n", what, e); exit(1); } }
static void current(EGLDisplay d, EGLSurface s, EGLContext c) { require(eglMakeCurrent(d, s, s, c), "eglMakeCurrent"); }
static GLuint generate_reused(GLuint old) {
    GLuint n = 0;
    for (int i = 0; i < 4096 && n != old; ++i) glGenBuffers(1, &n);
    gl_ok("generate reused name");
    if (n != old) { fprintf(stderr, "driver did not reuse name\n"); exit(3); }
    return n;
}
static void verify(struct model_context *c, int slot, int size, uint64_t generation) {
    GLenum target = slot ? GL_COPY_WRITE_BUFFER : GL_COPY_READ_BUFFER;
    GLenum pname = slot ? GL_COPY_WRITE_BUFFER_BINDING : GL_COPY_READ_BUFFER_BINDING;
    const struct model_object *o = model_bound(c, slot);
    GLint actual_name = -1, actual_size = 0;
    glGetIntegerv(pname, &actual_name);
    if (actual_name) glGetBufferParameteriv(target, GL_BUFFER_SIZE, &actual_size);
    gl_ok("query binding");
    require((GLuint)actual_name == model_name(o), "model name matches GLES");
    require(actual_size == size && actual_size == model_size(o), "model size matches GLES");
    require(model_generation(o) == generation, "expected model object generation");
}
int main(void) {
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor, count;
    const EGLint config_attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_NONE };
    EGLConfig config;
    require(eglInitialize(d, &major, &minor), "eglInitialize");
    require(eglChooseConfig(d, config_attrs, &config, 1, &count) && count == 1, "eglChooseConfig");
    const EGLint surf_attrs[] = { EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE };
    const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, config, surf_attrs);
    EGLContext a = eglCreateContext(d, config, EGL_NO_CONTEXT, ctx_attrs);
    EGLContext b = eglCreateContext(d, config, a, ctx_attrs);
    require(s != EGL_NO_SURFACE && a != EGL_NO_CONTEXT && b != EGL_NO_CONTEXT, "create shared contexts");
    struct model_group *g = model_group_new();
    require(g != NULL, "model group");
    struct model_context *ma = model_context_new(g), *mb = model_context_new(g);
    require(ma && mb, "model contexts");

    current(d, s, a);
    GLuint name = 0; glGenBuffers(1, &name); glBindBuffer(GL_COPY_WRITE_BUFFER, name);
    glBufferData(GL_COPY_WRITE_BUFFER, 64, NULL, GL_STATIC_DRAW); gl_ok("create old");
    require(model_generate(g, name, 64) && model_bind(ma, 1, name), "model old");
    verify(ma, 1, 64, 1);
    current(d, s, b);
    glDeleteBuffers(1, &name); model_delete(mb, name); gl_ok("delete old from other context");
    GLuint next = generate_reused(name);
    glBindBuffer(GL_COPY_READ_BUFFER, next); glBufferData(GL_COPY_READ_BUFFER, 128, NULL, GL_STATIC_DRAW); gl_ok("create new");
    require(model_generate(g, next, 128) && model_bind(mb, 0, next), "model new");
    verify(mb, 0, 128, 2);
    current(d, s, a); verify(ma, 1, 64, 1);
    glDeleteBuffers(1, &name); model_delete(ma, name); gl_ok("delete new while old bound");
    verify(ma, 1, 64, 1);
    current(d, s, b); verify(mb, 0, 128, 2);
    next = generate_reused(name);
    glBindBuffer(GL_COPY_WRITE_BUFFER, next); glBufferData(GL_COPY_WRITE_BUFFER, 256, NULL, GL_STATIC_DRAW); gl_ok("create third object");
    require(model_generate(g, next, 256) && model_bind(mb, 1, next), "model third");
    verify(mb, 0, 128, 2); verify(mb, 1, 256, 3);

    require(eglDestroyContext(d, a), "destroy original context"); model_context_destroy(ma);
    EGLContext c = eglCreateContext(d, config, b, ctx_attrs);
    struct model_context *mc = model_context_new(g);
    require(c != EGL_NO_CONTEXT && mc, "create context after original destruction");
    current(d, s, c);
    glBindBuffer(GL_COPY_READ_BUFFER, name); gl_ok("bind third in new context");
    require(model_bind(mc, 0, name), "model shared bind"); verify(mc, 0, 256, 3);
    current(d, s, b);
    glDeleteBuffers(1, &name); model_delete(mb, name); gl_ok("delete third from current context");
    verify(mb, 0, 128, 2); verify(mb, 1, 0, 0);
    current(d, s, c); verify(mc, 0, 256, 3);
    require(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "clear current context");
    require(eglDestroyContext(d, b), "destroy second context"); model_context_destroy(mb);
    require(eglDestroyContext(d, c), "destroy final context"); model_context_destroy(mc);
    eglDestroySurface(d, s); eglTerminate(d);
    puts("MODEL_MATCHES_GLES_BINDING_LIFETIME");
    return 0;
}
