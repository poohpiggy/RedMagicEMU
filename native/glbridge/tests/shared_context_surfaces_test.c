/* Run on Android ARM64 with the device's EGL driver. */
#include "../host.c"
#include <assert.h>

int main(void) {
    const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE
    };
    EGLint count = 0;
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(g_dpy && eglInitialize(g_dpy, NULL, NULL));
    assert(eglChooseConfig(g_dpy, attrs, &g_cfg, 1, &count) && count == 1);
    struct conn conn = {0};
    C = &conn;
    uint32_t parent = ctx_create(0, 3), child = ctx_create(parent, 3);
    assert(parent && child);

    assert(make_current(parent, 1, 32, 32, 0, 1));
    EGLContext parent_egl = eglGetCurrentContext();
    GLuint parent_color = C->surf->col;
    GLuint parent_fbo = C->surf->fbo;
    GLuint retired[6][4];
    for (uint32_t i = 0; i < 6; i++) {
        assert(make_current(child, i + 1, 912, 2016, i == 0 ? 4 : 0, 0));
        struct surf *s = C->surf;
        if (i == 0) assert(s->mscol && s->msds);
        retired[i][0] = s->col;
        retired[i][1] = s->rbo;
        retired[i][2] = s->mscol;
        retired[i][3] = s->msds;
        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    glFinish();
    assert(make_current(parent, 1, 32, 32, 0, 1));
    hd_other(OP_CTX_DESTROY, (const uint8_t *)&child, sizeof(child));

    assert(eglGetCurrentContext() == parent_egl && glIsRenderbuffer(parent_color));
    GLint bound = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
    assert((GLuint)bound == parent_fbo);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    uint8_t pixel[4];
    const uint8_t blue[] = {0, 0, 255, 255};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    assert(glGetError() == GL_NO_ERROR && memcmp(pixel, blue, sizeof pixel) == 0);
    unsigned remaining = 0;
    for (unsigned i = 0; i < 6; i++) {
        for (unsigned j = 0; j < 4; j++) {
            if (retired[i][j] && glIsRenderbuffer(retired[i][j])) remaining++;
        }
    }
    if (remaining) {
        fprintf(stderr, "%u renderbuffers remain after destroying the shared context\n", remaining);
        return 1;
    }

    uint32_t deferred = ctx_create(parent, 3);
    assert(deferred && make_current(deferred, 1, 64, 64, 0, 1));
    GLuint deferred_color = C->surf->col;
    hd_other(OP_CTX_DESTROY, (const uint8_t *)&deferred, sizeof(deferred));
    assert(eglGetCurrentContext() != EGL_NO_CONTEXT && glIsRenderbuffer(deferred_color));
    release_current();
    assert(eglGetCurrentContext() == EGL_NO_CONTEXT);
    assert(make_current(parent, 1, 32, 32, 0, 1));
    assert(glIsRenderbuffer(parent_color) && !glIsRenderbuffer(deferred_color));

    release_current();
    ctx_destroy(ctx_find(parent));
    eglTerminate(g_dpy);
    puts("Shared context destruction releases its surfaces and preserves the current context");
    return 0;
}
