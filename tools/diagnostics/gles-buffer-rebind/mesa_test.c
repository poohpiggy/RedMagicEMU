/* Native GLES comparison: shared old and new buffers can have the same name. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>

static void gl_ok(const char *where) {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) { fprintf(stderr, "%s: GL error 0x%x\n", where, e); exit(1); }
}
static void current(EGLDisplay d, EGLSurface s, EGLContext c) {
    if (!eglMakeCurrent(d, s, s, c)) { fprintf(stderr, "eglMakeCurrent failed\n"); exit(2); }
}
static int run(EGLDisplay d, EGLSurface s, EGLConfig config, GLenum target) {
    const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext a = eglCreateContext(d, config, EGL_NO_CONTEXT, ctx_attribs);
    EGLContext b = eglCreateContext(d, config, a, ctx_attribs);
    if (a == EGL_NO_CONTEXT || b == EGL_NO_CONTEXT) return 2;
    current(d, s, a);
    GLuint name;
    glGenBuffers(1, &name);
    glBindBuffer(target, name);
    glBufferData(target, 64, NULL, GL_DYNAMIC_DRAW);
    gl_ok("create old");
    current(d, s, b);
    glDeleteBuffers(1, &name);
    GLuint new_name = 0;
    for (int i = 0; i < 4096 && new_name != name; ++i) glGenBuffers(1, &new_name);
    gl_ok("delete and generate");
    if (new_name != name) { fprintf(stderr, "driver did not reuse name\n"); return 3; }
    glBindBuffer(target, new_name);
    glBufferData(target, 128, NULL, GL_DYNAMIC_DRAW);
    gl_ok("create new");
    current(d, s, a);
    GLint size = 0;
    glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    gl_ok("query old");
    if (size != 64) { fprintf(stderr, "old size %d, expected 64\n", size); return 1; }
    glBindBuffer(target, name);
    glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    gl_ok("rebind and query new");
    if (size != 128) { fprintf(stderr, "new size %d, expected 128\n", size); return 1; }
    printf("REBOUND target=0x%x name=%u old_size=64 new_size=128\n", target, name);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, b);
    eglDestroyContext(d, a);
    return 0;
}
static GLuint shader(GLenum type, const char *source) {
    GLuint obj = glCreateShader(type);
    glShaderSource(obj, 1, &source, NULL);
    glCompileShader(obj);
    GLint ok = 0;
    glGetShaderiv(obj, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(obj, sizeof(log), NULL, log); fprintf(stderr, "shader: %s\n", log); exit(1); }
    return obj;
}
static int red_pixels(void) {
    unsigned char pixels[8 * 8 * 4];
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    gl_ok("draw and read");
    int count = 0;
    for (size_t i = 0; i < sizeof(pixels); i += 4) if (pixels[i] > 128) ++count;
    return count;
}
static int run_attrib(EGLDisplay d, EGLSurface s, EGLConfig config) {
    const EGLint attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext a = eglCreateContext(d, config, EGL_NO_CONTEXT, attrs);
    EGLContext b = eglCreateContext(d, config, a, attrs);
    if (a == EGL_NO_CONTEXT || b == EGL_NO_CONTEXT) return 2;
    current(d, s, a);
    const char *vs = "#version 300 es\nlayout(location=0) in vec2 p; void main(){gl_Position=vec4(p,0.,1.);gl_PointSize=4.;}";
    const char *fs = "#version 300 es\nprecision mediump float; out vec4 c; void main(){c=vec4(1.,0.,0.,1.);}";
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
    GLuint program = glCreateProgram();
    glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
    GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) { fprintf(stderr, "program link failed\n"); return 1; }
    glUseProgram(program); glViewport(0, 0, 8, 8);
    glClearColor(0, 0, 0, 1);
    const float old_data[] = { 2.f, 2.f }, new_data[] = { 0.f, 0.f };
    GLuint name; glGenBuffers(1, &name);
    glBindBuffer(GL_ARRAY_BUFFER, name);
    glBufferData(GL_ARRAY_BUFFER, sizeof(old_data), old_data, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);
    gl_ok("prepare old attribute");
    current(d, s, b);
    glDeleteBuffers(1, &name);
    GLuint new_name = 0;
    for (int i = 0; i < 4096 && new_name != name; ++i) glGenBuffers(1, &new_name);
    gl_ok("delete and regenerate attribute buffer");
    if (new_name != name) { fprintf(stderr, "driver did not reuse name\n"); return 3; }
    glBindBuffer(GL_ARRAY_BUFFER, new_name);
    glBufferData(GL_ARRAY_BUFFER, sizeof(new_data), new_data, GL_STATIC_DRAW);
    gl_ok("prepare new attribute buffer");
    current(d, s, a);
    glBindBuffer(GL_ARRAY_BUFFER, name);
    int before = red_pixels();
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    int after = red_pixels();
    printf("ATTRIBUTE_REBOUND before_red=%d after_red=%d\n", before, after);
    if (before != 0 || after == 0) return 1;
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, b); eglDestroyContext(d, a);
    return 0;
}
int main(void) {
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor, count;
    const EGLint config_attribs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_NONE };
    EGLConfig config;
    if (!eglInitialize(d, &major, &minor) ||
        !eglChooseConfig(d, config_attribs, &config, 1, &count) || count != 1) return 2;
    const EGLint surface_attribs[] = { EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE };
    EGLSurface s = eglCreatePbufferSurface(d, config, surface_attribs);
    if (s == EGL_NO_SURFACE) return 2;
    int result = run(d, s, config, GL_ARRAY_BUFFER);
    if (!result) result = run(d, s, config, GL_ELEMENT_ARRAY_BUFFER);
    if (!result) result = run_attrib(d, s, config);
    eglDestroySurface(d, s);
    eglTerminate(d);
    return result;
}
