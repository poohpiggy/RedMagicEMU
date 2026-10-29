/* libhGLESv1_CM.so of the GLES 1 bridge (g1_fwd.c, gen1.py): Android 1.x libEGL loads libhEGL.so and
 * libhGLESv1_CM.so apart, each with RTLD_LOCAL; the driver's state is in libhEGL.so, so every GL entry point here
 * calls the one of the same name there (bionic of 1.x gives the already loaded library for the same name). */
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei, GLfixed, GLclampx, GLintptr, GLsizeiptr;
typedef float GLfloat, GLclampf;
typedef unsigned char GLboolean, GLubyte;
typedef signed char GLbyte;
typedef short GLshort;
typedef unsigned short GLushort;
typedef void GLvoid;
extern void *dlopen(const char *name, int flags);
extern void *dlsym(void *h, const char *name);
#define EXPORT __attribute__((visibility("default")))
static void *fwd_lib;
static void *fwd_sym(const char *name) {
    if (!fwd_lib) fwd_lib = dlopen("libhEGL.so", 0);
    void *f = fwd_lib ? dlsym(fwd_lib, name) : 0;
    if (!f) __builtin_trap();
    return f;
}
#define FWD(name, type) ({ static void *p_; if (!p_) p_ = fwd_sym(#name); (type p_); })
