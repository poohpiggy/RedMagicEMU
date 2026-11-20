/* Run on Android ARM64 with the device's EGL driver. */
#include "../host.c"
#include <assert.h>

static void check_surface(uint32_t context, uint32_t key, uint32_t samples, uint32_t color_only,
                          uint32_t payload_size, GLint expected_attachment) {
    uint32_t args[] = {context, key, 912, 2016, samples, color_only};
    C->rep_n = 0;
    hd_other(OP_MAKE_CURRENT, (const uint8_t *)args, payload_size);
    assert(C->rep_n == sizeof(uint32_t) && C->rep[0] == 1);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    if (samples) assert(C->surf->msfbo);

    GLint depth = -1, stencil = -1;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                           GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depth);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                           GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &stencil);
    if (depth != expected_attachment || stencil != expected_attachment) {
        fprintf(stderr, "samples=%u payload=%u: depth=%#x stencil=%#x, expected %#x\n",
                samples, payload_size, depth, stencil, expected_attachment);
        exit(1);
    }
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    resolve(C->surf);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, C->surf->fbo);
    uint8_t pixel[4];
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    static const uint8_t green[] = {0, 255, 0, 255};
    assert(glGetError() == GL_NO_ERROR && memcmp(pixel, green, sizeof pixel) == 0);
}

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
    uint32_t context = ctx_create(0, 3);
    assert(context);

    check_surface(context, 1, 0, 0, 5 * sizeof(uint32_t), GL_RENDERBUFFER);
    check_surface(context, 2, 0, 1, 6 * sizeof(uint32_t), GL_NONE);
    check_surface(context, 3, 4, 1, 6 * sizeof(uint32_t), GL_NONE);
    check_surface(context, 4, 0, 0, 6 * sizeof(uint32_t), GL_RENDERBUFFER);
    check_surface(context, 4, 0, 1, 6 * sizeof(uint32_t), GL_NONE);

    release_current();
    ctx_destroy(ctx_find(context));
    free(conn.rep);
    eglTerminate(g_dpy);
    puts("Color-only, multisampled, depth/stencil and legacy surface checks passed");
    return 0;
}
