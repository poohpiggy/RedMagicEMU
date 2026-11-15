/* Independent Guest diagnostic: render to an AHardwareBuffer-backed image. */
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define WIDTH 10
#define HEIGHT 6
#define USAGE 0x33u /* NX809J camera's observed CPU read/write usage */

static int failures;
static PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC get_native;
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target;

static int gl_ok(const char *where) {
    GLenum e = glGetError();
    if (e == GL_NO_ERROR) return 1;
    fprintf(stderr, "%s: GL error 0x%x\n", where, e);
    failures++;
    return 0;
}

static int color(const char *where, const uint8_t got[4], const uint8_t want[4]) {
    printf("%s: %u,%u,%u,%u\n", where, got[0], got[1], got[2], got[3]);
    if (memcmp(got, want, 4) == 0) return 1;
    fprintf(stderr, "%s: expected %u,%u,%u,%u\n", where, want[0], want[1], want[2], want[3]);
    failures++;
    return 0;
}

static GLuint shader(GLenum type, const char *source) {
    GLuint s = glCreateShader(type);
    if (!s) { fprintf(stderr, "glCreateShader failed\n"); failures++; return 0; }
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (ok && gl_ok("compile shader")) return s;
    char log[512] = {0};
    glGetShaderInfoLog(s, sizeof(log), NULL, log);
    fprintf(stderr, "shader compile failed: %s\n", log);
    failures++;
    glDeleteShader(s);
    return 0;
}

/* Each case owns a new buffer. The alias case is a positive control. */
static void run_case(EGLDisplay display, GLuint program, int generation, int alias_case) {
    const char *name = alias_case ? "2D alias control" : "external FBO";
    AHardwareBuffer *buffer = NULL;
    EGLImageKHR image = EGL_NO_IMAGE_KHR;
    GLuint external = 0, alias = 0, frame = 0, output = 0, output_frame = 0;
    void *mapped = NULL;
    int locked = 0;
    AHardwareBuffer_Desc desc = { .width = WIDTH, .height = HEIGHT, .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM, .usage = USAGE };
    int rc = AHardwareBuffer_allocate(&desc, &buffer);
    printf("generation %d: %s allocation=%d\n", generation, name, rc);
    if (rc || !buffer) { failures++; goto done; }

    /* Establish a known initial CPU image without assuming allocator contents. */
    AHardwareBuffer_describe(buffer, &desc);
    rc = AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, &mapped);
    locked = (rc == 0);
    if (rc || !mapped || desc.stride < WIDTH) { fprintf(stderr, "%s: initial CPU lock=%d\n", name, rc); failures++; goto done; }
    printf("allocation width=%u height=%u stride=%u\n", desc.width, desc.height, desc.stride);
    for (unsigned y = 0; y < HEIGHT; y++) memset((uint8_t *)mapped + (size_t)y * desc.stride * 4, 0, WIDTH * 4);
    rc = AHardwareBuffer_unlock(buffer, NULL);
    locked = 0;
    if (rc) { fprintf(stderr, "%s: initial CPU unlock=%d\n", name, rc); failures++; goto done; }

    const EGLint image_attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    image = create_image(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, get_native(buffer), image_attrs);
    if (image == EGL_NO_IMAGE_KHR) { fprintf(stderr, "%s: EGLImage creation error 0x%x\n", name, eglGetError()); failures++; goto done; }
    glGenTextures(1, &external);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, external);
    image_target(GL_TEXTURE_EXTERNAL_OES, image);
    if (!gl_ok("bind external EGLImage")) goto done;

    GLuint render_texture = external;
    GLenum render_target = GL_TEXTURE_EXTERNAL_OES;
    if (alias_case) {
        glGenTextures(1, &alias);
        glBindTexture(GL_TEXTURE_2D, alias);
        image_target(GL_TEXTURE_2D, image);
        if (!gl_ok("bind 2D EGLImage alias")) goto done;
        render_texture = alias;
        render_target = GL_TEXTURE_2D;
    }

    glGenFramebuffers(1, &frame);
    glBindFramebuffer(GL_FRAMEBUFFER, frame);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, render_target, render_texture, 0);
    GLenum attach_error = glGetError();
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("%s: attachment error=0x%x framebuffer=0x%x\n", name, attach_error, status);
    if (attach_error != GL_NO_ERROR || status != GL_FRAMEBUFFER_COMPLETE || !gl_ok("check framebuffer")) {
        fprintf(stderr, "%s: framebuffer attachment failed\n", name);
        failures++;
        goto done;
    }

    glGenTextures(1, &output);
    glBindTexture(GL_TEXTURE_2D, output);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &output_frame);
    glBindFramebuffer(GL_FRAMEBUFFER, output_frame);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (!gl_ok("create sampling target") || status != GL_FRAMEBUFFER_COMPLETE) { failures++; goto done; }

    const uint8_t colors[4][4] = {{255,0,0,255}, {0,255,0,255}, {0,0,255,255}, {255,255,0,255}};
    const unsigned coordinates[4][2] = {{1,1}, {8,1}, {1,4}, {8,4}};
    static const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    for (int i = 0; i < 2; i++) {
        uint8_t pixel[4] = {0};
        glBindFramebuffer(GL_FRAMEBUFFER, frame);
        glViewport(0, 0, WIDTH, HEIGHT);
        glEnable(GL_SCISSOR_TEST);
        for (int q = 0; q < 4; q++) {
            const uint8_t *c = colors[(q + i) % 4];
            glScissor((q & 1) * (WIDTH / 2), (q / 2) * (HEIGHT / 2), WIDTH / 2, HEIGHT / 2);
            glClearColor(c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);
        glFinish();
        for (int q = 0; q < 4; q++) {
            glReadPixels(coordinates[q][0], coordinates[q][1], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (!gl_ok("render/read framebuffer")) goto done;
            color("GPU quadrant", pixel, colors[(q + i) % 4]);
        }

        for (int repeat = 0; repeat < 2; repeat++) {
        AHardwareBuffer_Planes planes = {0};
        rc = AHardwareBuffer_lockPlanes(buffer, USAGE, -1, NULL, &planes);
        locked = (rc == 0);
        if (rc || planes.planeCount != 1 || !planes.planes[0].data ||
            planes.planes[0].pixelStride < 4 ||
            (uint64_t)planes.planes[0].rowStride < (uint64_t)(WIDTH - 1) * planes.planes[0].pixelStride + 4) {
            fprintf(stderr, "%s: lockPlanes=%d planes=%u\n", name, rc, planes.planeCount);
            failures++;
            goto done;
        }
        printf("lockPlanes rowStride=%u pixelStride=%u\n", planes.planes[0].rowStride, planes.planes[0].pixelStride);
        for (int q = 0; q < 4; q++) {
            const uint8_t *plane_pixel = (const uint8_t *)planes.planes[0].data +
                coordinates[q][1] * planes.planes[0].rowStride + coordinates[q][0] * planes.planes[0].pixelStride;
            memcpy(pixel, plane_pixel, 4);
            color(repeat ? "planes repeat quadrant" : "planes quadrant", pixel, colors[(q + i) % 4]);
        }
        rc = AHardwareBuffer_unlock(buffer, NULL);
        locked = 0;
        if (rc) { fprintf(stderr, "%s: planes read unlock=%d\n", name, rc); failures++; goto done; }
        }

        rc = AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, NULL, &mapped);
        locked = (rc == 0);
        if (rc || !mapped) { fprintf(stderr, "%s: CPU read lock=%d\n", name, rc); failures++; goto done; }
        for (int q = 0; q < 4; q++) {
            const uint8_t *p = (const uint8_t *)mapped + ((size_t)coordinates[q][1] * desc.stride + coordinates[q][0]) * 4;
            memcpy(pixel, p, 4);
            color("CPU quadrant", pixel, colors[(q + i) % 4]);
        }
        rc = AHardwareBuffer_unlock(buffer, NULL);
        locked = 0;
        if (rc) { fprintf(stderr, "%s: CPU read unlock=%d\n", name, rc); failures++; goto done; }

        glBindFramebuffer(GL_FRAMEBUFFER, output_frame);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, external);
        glUniform1i(glGetUniformLocation(program, "tex"), 0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glFinish();
        for (int q = 0; q < 4; q++) {
            glReadPixels(coordinates[q][0], coordinates[q][1], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (!gl_ok("sample external texture")) goto done;
            color("OES quadrant", pixel, colors[(q + i) % 4]);
        }
    }

done:
    if (locked && AHardwareBuffer_unlock(buffer, NULL)) { fprintf(stderr, "%s: cleanup unlock failed\n", name); failures++; }
    if (frame) glDeleteFramebuffers(1, &frame);
    if (output_frame) glDeleteFramebuffers(1, &output_frame);
    if (external) glDeleteTextures(1, &external);
    if (alias) glDeleteTextures(1, &alias);
    if (output) glDeleteTextures(1, &output);
    if (!gl_ok("cleanup GL objects")) fprintf(stderr, "%s: cleanup failed\n", name);
    if (image != EGL_NO_IMAGE_KHR && !destroy_image(display, image)) { fprintf(stderr, "%s: destroy EGLImage failed\n", name); failures++; }
    if (buffer) AHardwareBuffer_release(buffer);
}

int main(void) {
    setbuf(stdout, NULL);
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint vertex = 0, fragment = 0, program = 0;
    int initialized = 0;
    EGLint count = 0, major = 0, minor = 0;
    EGLConfig config;
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    const EGLint surface_attrs[] = {EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE};
    const EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) { fprintf(stderr, "EGL initialization error 0x%x\n", eglGetError()); failures++; goto done; }
    initialized = 1;
    if (!eglChooseConfig(display, attrs, &config, 1, &count) || count != 1) { fprintf(stderr, "EGL config error 0x%x\n", eglGetError()); failures++; goto done; }
    surface = eglCreatePbufferSurface(display, config, surface_attrs);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, context)) { fprintf(stderr, "EGL context error 0x%x\n", eglGetError()); failures++; goto done; }
    printf("EGL %d.%d renderer=%s\n", major, minor, glGetString(GL_RENDERER));
    get_native = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)eglGetProcAddress("eglGetNativeClientBufferANDROID");
    create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    image_target = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!get_native || !create_image || !destroy_image || !image_target) { fprintf(stderr, "missing EGLImage entry point\n"); failures++; goto done; }
    vertex = shader(GL_VERTEX_SHADER, "attribute vec2 p; varying vec2 uv; void main(){gl_Position=vec4(p,0.,1.);uv=p*.5+.5;}");
    fragment = shader(GL_FRAGMENT_SHADER, "#extension GL_OES_EGL_image_external : require\nprecision mediump float; varying vec2 uv; uniform samplerExternalOES tex; void main(){gl_FragColor=texture2D(tex,uv);}");
    if (!vertex || !fragment) goto done;
    program = glCreateProgram();
    if (!program) { fprintf(stderr, "glCreateProgram failed\n"); failures++; goto done; }
    glAttachShader(program, vertex); glAttachShader(program, fragment); glBindAttribLocation(program, 0, "p"); glLinkProgram(program);
    GLint linked = GL_FALSE; glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked || !gl_ok("link program")) { fprintf(stderr, "program link failed\n"); failures++; goto done; }
    for (int generation = 1; generation <= 2; generation++) {
        run_case(display, program, generation, 0);
        run_case(display, program, generation, 1);
    }
done:
    if (program) glDeleteProgram(program);
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    if (context != EGL_NO_CONTEXT) {
        if (!gl_ok("cleanup program")) fprintf(stderr, "program cleanup failed\n");
        if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) { fprintf(stderr, "release EGL context failed\n"); failures++; }
        if (!eglDestroyContext(display, context)) { fprintf(stderr, "destroy EGL context failed\n"); failures++; }
    }
    if (surface != EGL_NO_SURFACE && !eglDestroySurface(display, surface)) { fprintf(stderr, "destroy EGL surface failed\n"); failures++; }
    if (initialized && !eglTerminate(display)) { fprintf(stderr, "terminate EGL failed\n"); failures++; }
    printf("diagnostic failures=%d\n", failures);
    return failures ? 1 : 0;
}
