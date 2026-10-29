/* written by gen1.py */
EXPORT void glAlphaFunc(GLenum func, GLfloat ref) {
    uint8_t *q_ = enc1(IDX1_glAlphaFunc, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)func);
    q_ = putf(q_, ref);
}
EXPORT void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    uint8_t *q_ = enc1(IDX1_glClearColor, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = putf(q_, red);
    q_ = putf(q_, green);
    q_ = putf(q_, blue);
    q_ = putf(q_, alpha);
}
EXPORT void glClearDepthf(GLfloat d) {
    uint8_t *q_ = enc1(IDX1_glClearDepthf, 4);
    if (!q_) return;
    q_ = putf(q_, d);
}
EXPORT void glClipPlanef(GLenum p, const GLfloat * eqn) {
    uint8_t *q_ = enc1(IDX1_glClipPlanef, 4 + BLOB1(eqn ? (uint32_t)(16) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)p);
    q_ = putblob(q_, eqn, eqn ? (uint32_t)(16) : NIL);
}
EXPORT void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    uint8_t *q_ = enc1(IDX1_glColor4f, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = putf(q_, red);
    q_ = putf(q_, green);
    q_ = putf(q_, blue);
    q_ = putf(q_, alpha);
}
EXPORT void glDepthRangef(GLfloat n, GLfloat f) {
    uint8_t *q_ = enc1(IDX1_glDepthRangef, 4 + 4);
    if (!q_) return;
    q_ = putf(q_, n);
    q_ = putf(q_, f);
}
EXPORT void glFogf(GLenum pname, GLfloat param) {
    uint8_t *q_ = enc1(IDX1_glFogf, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = putf(q_, param);
}
EXPORT void glFogfv(GLenum pname, const GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glFogfv, 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x0B66 ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x0B66 ? 4 : 1)) : NIL);
}
EXPORT void glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
    uint8_t *q_ = enc1(IDX1_glFrustumf, 4 + 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = putf(q_, l);
    q_ = putf(q_, r);
    q_ = putf(q_, b);
    q_ = putf(q_, t);
    q_ = putf(q_, n);
    q_ = putf(q_, f);
}
EXPORT void glGetClipPlanef(GLenum plane, GLfloat * equation) {
    uint8_t *q_ = enc1(IDX1_glGetClipPlanef, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)plane);
    if (!call1()) return;
    get1(equation, (uint32_t)(16));
}
EXPORT void glGetLightfv(GLenum light, GLenum pname, GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glGetLightfv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)light);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * gl1_light_n(pname)));
}
EXPORT void glGetMaterialfv(GLenum face, GLenum pname, GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glGetMaterialfv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)face);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * gl1_material_n(pname)));
}
EXPORT void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glGetTexEnvfv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * (pname == 0x2201 ? 4 : 1)));
}
EXPORT void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glGetTexParameterfv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * (pname == 0x8B9D ? 4 : 1)));
}
EXPORT void glLightModelf(GLenum pname, GLfloat param) {
    uint8_t *q_ = enc1(IDX1_glLightModelf, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = putf(q_, param);
}
EXPORT void glLightModelfv(GLenum pname, const GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glLightModelfv, 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x0B53 ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x0B53 ? 4 : 1)) : NIL);
}
EXPORT void glLightf(GLenum light, GLenum pname, GLfloat param) {
    uint8_t *q_ = enc1(IDX1_glLightf, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)light);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putf(q_, param);
}
EXPORT void glLightfv(GLenum light, GLenum pname, const GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glLightfv, 4 + 4 + BLOB1(params ? (uint32_t)(4 * gl1_light_n(pname)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)light);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * gl1_light_n(pname)) : NIL);
}
EXPORT void glLineWidth(GLfloat width) {
    uint8_t *q_ = enc1(IDX1_glLineWidth, 4);
    if (!q_) return;
    q_ = putf(q_, width);
}
EXPORT void glLoadMatrixf(const GLfloat * m) {
    uint8_t *q_ = enc1(IDX1_glLoadMatrixf, BLOB1(m ? (uint32_t)(64) : NIL));
    if (!q_) return;
    q_ = putblob(q_, m, m ? (uint32_t)(64) : NIL);
}
EXPORT void glMaterialf(GLenum face, GLenum pname, GLfloat param) {
    uint8_t *q_ = enc1(IDX1_glMaterialf, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)face);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putf(q_, param);
}
EXPORT void glMaterialfv(GLenum face, GLenum pname, const GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glMaterialfv, 4 + 4 + BLOB1(params ? (uint32_t)(4 * gl1_material_n(pname)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)face);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * gl1_material_n(pname)) : NIL);
}
EXPORT void glMultMatrixf(const GLfloat * m) {
    uint8_t *q_ = enc1(IDX1_glMultMatrixf, BLOB1(m ? (uint32_t)(64) : NIL));
    if (!q_) return;
    q_ = putblob(q_, m, m ? (uint32_t)(64) : NIL);
}
EXPORT void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    uint8_t *q_ = enc1(IDX1_glMultiTexCoord4f, 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = putf(q_, s);
    q_ = putf(q_, t);
    q_ = putf(q_, r);
    q_ = putf(q_, q);
}
EXPORT void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) {
    uint8_t *q_ = enc1(IDX1_glNormal3f, 4 + 4 + 4);
    if (!q_) return;
    q_ = putf(q_, nx);
    q_ = putf(q_, ny);
    q_ = putf(q_, nz);
}
EXPORT void glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) {
    uint8_t *q_ = enc1(IDX1_glOrthof, 4 + 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = putf(q_, l);
    q_ = putf(q_, r);
    q_ = putf(q_, b);
    q_ = putf(q_, t);
    q_ = putf(q_, n);
    q_ = putf(q_, f);
}
EXPORT void glPointParameterf(GLenum pname, GLfloat param) {
    uint8_t *q_ = enc1(IDX1_glPointParameterf, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = putf(q_, param);
}
EXPORT void glPointParameterfv(GLenum pname, const GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glPointParameterfv, 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x8129 ? 3 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x8129 ? 3 : 1)) : NIL);
}
EXPORT void glPointSize(GLfloat size) {
    uint8_t *q_ = enc1(IDX1_glPointSize, 4);
    if (!q_) return;
    q_ = putf(q_, size);
}
EXPORT void glPolygonOffset(GLfloat factor, GLfloat units) {
    uint8_t *q_ = enc1(IDX1_glPolygonOffset, 4 + 4);
    if (!q_) return;
    q_ = putf(q_, factor);
    q_ = putf(q_, units);
}
EXPORT void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    uint8_t *q_ = enc1(IDX1_glRotatef, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = putf(q_, angle);
    q_ = putf(q_, x);
    q_ = putf(q_, y);
    q_ = putf(q_, z);
}
EXPORT void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    uint8_t *q_ = enc1(IDX1_glScalef, 4 + 4 + 4);
    if (!q_) return;
    q_ = putf(q_, x);
    q_ = putf(q_, y);
    q_ = putf(q_, z);
}
EXPORT void glTexEnvf(GLenum target, GLenum pname, GLfloat param) {
    uint8_t *q_ = enc1(IDX1_glTexEnvf, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putf(q_, param);
}
EXPORT void glTexEnvfv(GLenum target, GLenum pname, const GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glTexEnvfv, 4 + 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x2201 ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x2201 ? 4 : 1)) : NIL);
}
EXPORT void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    uint8_t *q_ = enc1(IDX1_glTexParameterf, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putf(q_, param);
}
EXPORT void glTexParameterfv(GLenum target, GLenum pname, const GLfloat * params) {
    uint8_t *q_ = enc1(IDX1_glTexParameterfv, 4 + 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x8B9D ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x8B9D ? 4 : 1)) : NIL);
}
EXPORT void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    uint8_t *q_ = enc1(IDX1_glTranslatef, 4 + 4 + 4);
    if (!q_) return;
    q_ = putf(q_, x);
    q_ = putf(q_, y);
    q_ = putf(q_, z);
}
EXPORT void glActiveTexture(GLenum texture) {
    uint8_t *q_ = enc1(IDX1_glActiveTexture, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)texture);
}
EXPORT void glAlphaFuncx(GLenum func, GLfixed ref) {
    uint8_t *q_ = enc1(IDX1_glAlphaFuncx, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)func);
    q_ = put32(q_, (uint32_t)ref);
}
EXPORT void glBindBuffer(GLenum target, GLuint buffer) {
    k1_glBindBuffer(target, buffer);
    uint8_t *q_ = enc1(IDX1_glBindBuffer, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)buffer);
}
EXPORT void glBindTexture(GLenum target, GLuint texture) {
    uint8_t *q_ = enc1(IDX1_glBindTexture, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)texture);
}
EXPORT void glBlendFunc(GLenum sfactor, GLenum dfactor) {
    uint8_t *q_ = enc1(IDX1_glBlendFunc, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)sfactor);
    q_ = put32(q_, (uint32_t)dfactor);
}
EXPORT void glClear(GLbitfield mask) {
    uint8_t *q_ = enc1(IDX1_glClear, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)mask);
}
EXPORT void glClearColorx(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) {
    uint8_t *q_ = enc1(IDX1_glClearColorx, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)red);
    q_ = put32(q_, (uint32_t)green);
    q_ = put32(q_, (uint32_t)blue);
    q_ = put32(q_, (uint32_t)alpha);
}
EXPORT void glClearDepthx(GLfixed depth) {
    uint8_t *q_ = enc1(IDX1_glClearDepthx, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)depth);
}
EXPORT void glClearStencil(GLint s) {
    uint8_t *q_ = enc1(IDX1_glClearStencil, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)s);
}
EXPORT void glClipPlanex(GLenum plane, const GLfixed * equation) {
    uint8_t *q_ = enc1(IDX1_glClipPlanex, 4 + BLOB1(equation ? (uint32_t)(16) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)plane);
    q_ = putblob(q_, equation, equation ? (uint32_t)(16) : NIL);
}
EXPORT void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha) {
    uint8_t *q_ = enc1(IDX1_glColor4ub, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)red);
    q_ = put32(q_, (uint32_t)green);
    q_ = put32(q_, (uint32_t)blue);
    q_ = put32(q_, (uint32_t)alpha);
}
EXPORT void glColor4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) {
    uint8_t *q_ = enc1(IDX1_glColor4x, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)red);
    q_ = put32(q_, (uint32_t)green);
    q_ = put32(q_, (uint32_t)blue);
    q_ = put32(q_, (uint32_t)alpha);
}
EXPORT void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
    uint8_t *q_ = enc1(IDX1_glColorMask, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)red);
    q_ = put32(q_, (uint32_t)green);
    q_ = put32(q_, (uint32_t)blue);
    q_ = put32(q_, (uint32_t)alpha);
}
EXPORT void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void * data) {
    uint8_t *q_ = enc1(IDX1_glCompressedTexImage2D, 4 + 4 + 4 + 4 + 4 + 4 + 4 + BLOB1(data ? (uint32_t)(imageSize) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)level);
    q_ = put32(q_, (uint32_t)internalformat);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
    q_ = put32(q_, (uint32_t)border);
    q_ = put32(q_, (uint32_t)imageSize);
    q_ = putblob(q_, data, data ? (uint32_t)(imageSize) : NIL);
}
EXPORT void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void * data) {
    uint8_t *q_ = enc1(IDX1_glCompressedTexSubImage2D, 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + BLOB1(data ? (uint32_t)(imageSize) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)level);
    q_ = put32(q_, (uint32_t)xoffset);
    q_ = put32(q_, (uint32_t)yoffset);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
    q_ = put32(q_, (uint32_t)format);
    q_ = put32(q_, (uint32_t)imageSize);
    q_ = putblob(q_, data, data ? (uint32_t)(imageSize) : NIL);
}
EXPORT void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) {
    uint8_t *q_ = enc1(IDX1_glCopyTexImage2D, 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)level);
    q_ = put32(q_, (uint32_t)internalformat);
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
    q_ = put32(q_, (uint32_t)border);
}
EXPORT void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) {
    uint8_t *q_ = enc1(IDX1_glCopyTexSubImage2D, 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)level);
    q_ = put32(q_, (uint32_t)xoffset);
    q_ = put32(q_, (uint32_t)yoffset);
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
}
EXPORT void glCullFace(GLenum mode) {
    uint8_t *q_ = enc1(IDX1_glCullFace, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)mode);
}
EXPORT void glDeleteBuffers(GLsizei n, const GLuint * buffers) {
    k1_glDeleteBuffers(n, buffers);
    uint8_t *q_ = enc1(IDX1_glDeleteBuffers, 4 + BLOB1(buffers ? (uint32_t)(n * 4) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)n);
    q_ = putblob(q_, buffers, buffers ? (uint32_t)(n * 4) : NIL);
}
EXPORT void glDeleteTextures(GLsizei n, const GLuint * textures) {
    uint8_t *q_ = enc1(IDX1_glDeleteTextures, 4 + BLOB1(textures ? (uint32_t)(n * 4) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)n);
    q_ = putblob(q_, textures, textures ? (uint32_t)(n * 4) : NIL);
}
EXPORT void glDepthFunc(GLenum func) {
    uint8_t *q_ = enc1(IDX1_glDepthFunc, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)func);
}
EXPORT void glDepthMask(GLboolean flag) {
    uint8_t *q_ = enc1(IDX1_glDepthMask, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)flag);
}
EXPORT void glDepthRangex(GLfixed n, GLfixed f) {
    uint8_t *q_ = enc1(IDX1_glDepthRangex, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)n);
    q_ = put32(q_, (uint32_t)f);
}
EXPORT void glDisable(GLenum cap) {
    uint8_t *q_ = enc1(IDX1_glDisable, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)cap);
}
EXPORT void glEnable(GLenum cap) {
    uint8_t *q_ = enc1(IDX1_glEnable, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)cap);
}
EXPORT void glFlush(void) {
    uint8_t *q_ = enc1(IDX1_glFlush, 0);
    if (!q_) return;
}
EXPORT void glFogx(GLenum pname, GLfixed param) {
    uint8_t *q_ = enc1(IDX1_glFogx, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glFogxv(GLenum pname, const GLfixed * param) {
    uint8_t *q_ = enc1(IDX1_glFogxv, 4 + BLOB1(param ? (uint32_t)(4 * (pname == 0x0B66 ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, param, param ? (uint32_t)(4 * (pname == 0x0B66 ? 4 : 1)) : NIL);
}
EXPORT void glFrontFace(GLenum mode) {
    uint8_t *q_ = enc1(IDX1_glFrontFace, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)mode);
}
EXPORT void glFrustumx(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) {
    uint8_t *q_ = enc1(IDX1_glFrustumx, 4 + 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)l);
    q_ = put32(q_, (uint32_t)r);
    q_ = put32(q_, (uint32_t)b);
    q_ = put32(q_, (uint32_t)t);
    q_ = put32(q_, (uint32_t)n);
    q_ = put32(q_, (uint32_t)f);
}
EXPORT void glGetBufferParameteriv(GLenum target, GLenum pname, GLint * params) {
    uint8_t *q_ = enc1(IDX1_glGetBufferParameteriv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4));
}
EXPORT void glGetClipPlanex(GLenum plane, GLfixed * equation) {
    uint8_t *q_ = enc1(IDX1_glGetClipPlanex, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)plane);
    if (!call1()) return;
    get1(equation, (uint32_t)(16));
}
EXPORT void glGenBuffers(GLsizei n, GLuint * buffers) {
    uint8_t *q_ = enc1(IDX1_glGenBuffers, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)n);
    if (!call1()) return;
    get1(buffers, (uint32_t)(n * 4));
}
EXPORT void glGenTextures(GLsizei n, GLuint * textures) {
    uint8_t *q_ = enc1(IDX1_glGenTextures, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)n);
    if (!call1()) return;
    get1(textures, (uint32_t)(n * 4));
}
EXPORT GLenum glGetError(void) {
    uint8_t *q_ = enc1(IDX1_glGetError, 0);
    if (!q_) return (GLenum)0;
    if (!call1()) return (GLenum)0;
    return (GLenum)get1_32();
}
EXPORT void glGetLightxv(GLenum light, GLenum pname, GLfixed * params) {
    uint8_t *q_ = enc1(IDX1_glGetLightxv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)light);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * gl1_light_n(pname)));
}
EXPORT void glGetMaterialxv(GLenum face, GLenum pname, GLfixed * params) {
    uint8_t *q_ = enc1(IDX1_glGetMaterialxv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)face);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * gl1_material_n(pname)));
}
EXPORT void glGetTexEnviv(GLenum target, GLenum pname, GLint * params) {
    uint8_t *q_ = enc1(IDX1_glGetTexEnviv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * (pname == 0x2201 ? 4 : 1)));
}
EXPORT void glGetTexEnvxv(GLenum target, GLenum pname, GLfixed * params) {
    uint8_t *q_ = enc1(IDX1_glGetTexEnvxv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * (pname == 0x2201 ? 4 : 1)));
}
EXPORT void glGetTexParameteriv(GLenum target, GLenum pname, GLint * params) {
    uint8_t *q_ = enc1(IDX1_glGetTexParameteriv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * (pname == 0x8B9D ? 4 : 1)));
}
EXPORT void glGetTexParameterxv(GLenum target, GLenum pname, GLfixed * params) {
    uint8_t *q_ = enc1(IDX1_glGetTexParameterxv, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4 * (pname == 0x8B9D ? 4 : 1)));
}
EXPORT void glHint(GLenum target, GLenum mode) {
    uint8_t *q_ = enc1(IDX1_glHint, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)mode);
}
EXPORT GLboolean glIsBuffer(GLuint buffer) {
    uint8_t *q_ = enc1(IDX1_glIsBuffer, 4);
    if (!q_) return (GLboolean)0;
    q_ = put32(q_, (uint32_t)buffer);
    if (!call1()) return (GLboolean)0;
    return (GLboolean)get1_32();
}
EXPORT GLboolean glIsTexture(GLuint texture) {
    uint8_t *q_ = enc1(IDX1_glIsTexture, 4);
    if (!q_) return (GLboolean)0;
    q_ = put32(q_, (uint32_t)texture);
    if (!call1()) return (GLboolean)0;
    return (GLboolean)get1_32();
}
EXPORT void glLightModelx(GLenum pname, GLfixed param) {
    uint8_t *q_ = enc1(IDX1_glLightModelx, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glLightModelxv(GLenum pname, const GLfixed * param) {
    uint8_t *q_ = enc1(IDX1_glLightModelxv, 4 + BLOB1(param ? (uint32_t)(4 * (pname == 0x0B53 ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, param, param ? (uint32_t)(4 * (pname == 0x0B53 ? 4 : 1)) : NIL);
}
EXPORT void glLightx(GLenum light, GLenum pname, GLfixed param) {
    uint8_t *q_ = enc1(IDX1_glLightx, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)light);
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glLightxv(GLenum light, GLenum pname, const GLfixed * params) {
    uint8_t *q_ = enc1(IDX1_glLightxv, 4 + 4 + BLOB1(params ? (uint32_t)(4 * gl1_light_n(pname)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)light);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * gl1_light_n(pname)) : NIL);
}
EXPORT void glLineWidthx(GLfixed width) {
    uint8_t *q_ = enc1(IDX1_glLineWidthx, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)width);
}
EXPORT void glLoadIdentity(void) {
    uint8_t *q_ = enc1(IDX1_glLoadIdentity, 0);
    if (!q_) return;
}
EXPORT void glLoadMatrixx(const GLfixed * m) {
    uint8_t *q_ = enc1(IDX1_glLoadMatrixx, BLOB1(m ? (uint32_t)(64) : NIL));
    if (!q_) return;
    q_ = putblob(q_, m, m ? (uint32_t)(64) : NIL);
}
EXPORT void glLogicOp(GLenum opcode) {
    uint8_t *q_ = enc1(IDX1_glLogicOp, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)opcode);
}
EXPORT void glMaterialx(GLenum face, GLenum pname, GLfixed param) {
    uint8_t *q_ = enc1(IDX1_glMaterialx, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)face);
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glMaterialxv(GLenum face, GLenum pname, const GLfixed * param) {
    uint8_t *q_ = enc1(IDX1_glMaterialxv, 4 + 4 + BLOB1(param ? (uint32_t)(4 * gl1_material_n(pname)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)face);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, param, param ? (uint32_t)(4 * gl1_material_n(pname)) : NIL);
}
EXPORT void glMatrixMode(GLenum mode) {
    uint8_t *q_ = enc1(IDX1_glMatrixMode, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)mode);
}
EXPORT void glMultMatrixx(const GLfixed * m) {
    uint8_t *q_ = enc1(IDX1_glMultMatrixx, BLOB1(m ? (uint32_t)(64) : NIL));
    if (!q_) return;
    q_ = putblob(q_, m, m ? (uint32_t)(64) : NIL);
}
EXPORT void glMultiTexCoord4x(GLenum texture, GLfixed s, GLfixed t, GLfixed r, GLfixed q) {
    uint8_t *q_ = enc1(IDX1_glMultiTexCoord4x, 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)texture);
    q_ = put32(q_, (uint32_t)s);
    q_ = put32(q_, (uint32_t)t);
    q_ = put32(q_, (uint32_t)r);
    q_ = put32(q_, (uint32_t)q);
}
EXPORT void glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz) {
    uint8_t *q_ = enc1(IDX1_glNormal3x, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)nx);
    q_ = put32(q_, (uint32_t)ny);
    q_ = put32(q_, (uint32_t)nz);
}
EXPORT void glOrthox(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) {
    uint8_t *q_ = enc1(IDX1_glOrthox, 4 + 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)l);
    q_ = put32(q_, (uint32_t)r);
    q_ = put32(q_, (uint32_t)b);
    q_ = put32(q_, (uint32_t)t);
    q_ = put32(q_, (uint32_t)n);
    q_ = put32(q_, (uint32_t)f);
}
EXPORT void glPixelStorei(GLenum pname, GLint param) {
    k1_glPixelStorei(pname, param);
    uint8_t *q_ = enc1(IDX1_glPixelStorei, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glPointParameterx(GLenum pname, GLfixed param) {
    uint8_t *q_ = enc1(IDX1_glPointParameterx, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glPointParameterxv(GLenum pname, const GLfixed * params) {
    uint8_t *q_ = enc1(IDX1_glPointParameterxv, 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x8129 ? 3 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x8129 ? 3 : 1)) : NIL);
}
EXPORT void glPointSizex(GLfixed size) {
    uint8_t *q_ = enc1(IDX1_glPointSizex, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)size);
}
EXPORT void glPolygonOffsetx(GLfixed factor, GLfixed units) {
    uint8_t *q_ = enc1(IDX1_glPolygonOffsetx, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)factor);
    q_ = put32(q_, (uint32_t)units);
}
EXPORT void glPopMatrix(void) {
    uint8_t *q_ = enc1(IDX1_glPopMatrix, 0);
    if (!q_) return;
}
EXPORT void glPushMatrix(void) {
    uint8_t *q_ = enc1(IDX1_glPushMatrix, 0);
    if (!q_) return;
}
EXPORT void glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z) {
    uint8_t *q_ = enc1(IDX1_glRotatex, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)angle);
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)z);
}
EXPORT void glSampleCoverage(GLfloat value, GLboolean invert) {
    uint8_t *q_ = enc1(IDX1_glSampleCoverage, 4 + 4);
    if (!q_) return;
    q_ = putf(q_, value);
    q_ = put32(q_, (uint32_t)invert);
}
EXPORT void glSampleCoveragex(GLclampx value, GLboolean invert) {
    uint8_t *q_ = enc1(IDX1_glSampleCoveragex, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)value);
    q_ = put32(q_, (uint32_t)invert);
}
EXPORT void glScalex(GLfixed x, GLfixed y, GLfixed z) {
    uint8_t *q_ = enc1(IDX1_glScalex, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)z);
}
EXPORT void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    uint8_t *q_ = enc1(IDX1_glScissor, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
}
EXPORT void glShadeModel(GLenum mode) {
    uint8_t *q_ = enc1(IDX1_glShadeModel, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)mode);
}
EXPORT void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    uint8_t *q_ = enc1(IDX1_glStencilFunc, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)func);
    q_ = put32(q_, (uint32_t)ref);
    q_ = put32(q_, (uint32_t)mask);
}
EXPORT void glStencilMask(GLuint mask) {
    uint8_t *q_ = enc1(IDX1_glStencilMask, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)mask);
}
EXPORT void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    uint8_t *q_ = enc1(IDX1_glStencilOp, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)fail);
    q_ = put32(q_, (uint32_t)zfail);
    q_ = put32(q_, (uint32_t)zpass);
}
EXPORT void glTexEnvi(GLenum target, GLenum pname, GLint param) {
    uint8_t *q_ = enc1(IDX1_glTexEnvi, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glTexEnvx(GLenum target, GLenum pname, GLfixed param) {
    uint8_t *q_ = enc1(IDX1_glTexEnvx, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glTexEnviv(GLenum target, GLenum pname, const GLint * params) {
    uint8_t *q_ = enc1(IDX1_glTexEnviv, 4 + 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x2201 ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x2201 ? 4 : 1)) : NIL);
}
EXPORT void glTexEnvxv(GLenum target, GLenum pname, const GLfixed * params) {
    uint8_t *q_ = enc1(IDX1_glTexEnvxv, 4 + 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x2201 ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x2201 ? 4 : 1)) : NIL);
}
EXPORT void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    uint8_t *q_ = enc1(IDX1_glTexParameteri, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glTexParameterx(GLenum target, GLenum pname, GLfixed param) {
    uint8_t *q_ = enc1(IDX1_glTexParameterx, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = put32(q_, (uint32_t)param);
}
EXPORT void glTexParameteriv(GLenum target, GLenum pname, const GLint * params) {
    uint8_t *q_ = enc1(IDX1_glTexParameteriv, 4 + 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x8B9D ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x8B9D ? 4 : 1)) : NIL);
}
EXPORT void glTexParameterxv(GLenum target, GLenum pname, const GLfixed * params) {
    uint8_t *q_ = enc1(IDX1_glTexParameterxv, 4 + 4 + BLOB1(params ? (uint32_t)(4 * (pname == 0x8B9D ? 4 : 1)) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    q_ = putblob(q_, params, params ? (uint32_t)(4 * (pname == 0x8B9D ? 4 : 1)) : NIL);
}
EXPORT void glTranslatex(GLfixed x, GLfixed y, GLfixed z) {
    uint8_t *q_ = enc1(IDX1_glTranslatex, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)z);
}
EXPORT void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    uint8_t *q_ = enc1(IDX1_glViewport, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
}
EXPORT void glBlendEquationSeparateOES(GLenum modeRGB, GLenum modeAlpha) {
    uint8_t *q_ = enc1(IDX1_glBlendEquationSeparateOES, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)modeRGB);
    q_ = put32(q_, (uint32_t)modeAlpha);
}
EXPORT void glBlendFuncSeparateOES(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) {
    uint8_t *q_ = enc1(IDX1_glBlendFuncSeparateOES, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)srcRGB);
    q_ = put32(q_, (uint32_t)dstRGB);
    q_ = put32(q_, (uint32_t)srcAlpha);
    q_ = put32(q_, (uint32_t)dstAlpha);
}
EXPORT void glBlendEquationOES(GLenum mode) {
    uint8_t *q_ = enc1(IDX1_glBlendEquationOES, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)mode);
}
EXPORT void glDrawTexsOES(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height) {
    uint8_t *q_ = enc1(IDX1_glDrawTexsOES, 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)z);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
}
EXPORT void glDrawTexiOES(GLint x, GLint y, GLint z, GLint width, GLint height) {
    uint8_t *q_ = enc1(IDX1_glDrawTexiOES, 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)z);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
}
EXPORT void glDrawTexxOES(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height) {
    uint8_t *q_ = enc1(IDX1_glDrawTexxOES, 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)x);
    q_ = put32(q_, (uint32_t)y);
    q_ = put32(q_, (uint32_t)z);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
}
EXPORT void glDrawTexsvOES(const GLshort * coords) {
    uint8_t *q_ = enc1(IDX1_glDrawTexsvOES, BLOB1(coords ? (uint32_t)(10) : NIL));
    if (!q_) return;
    q_ = putblob(q_, coords, coords ? (uint32_t)(10) : NIL);
}
EXPORT void glDrawTexivOES(const GLint * coords) {
    uint8_t *q_ = enc1(IDX1_glDrawTexivOES, BLOB1(coords ? (uint32_t)(20) : NIL));
    if (!q_) return;
    q_ = putblob(q_, coords, coords ? (uint32_t)(20) : NIL);
}
EXPORT void glDrawTexxvOES(const GLfixed * coords) {
    uint8_t *q_ = enc1(IDX1_glDrawTexxvOES, BLOB1(coords ? (uint32_t)(20) : NIL));
    if (!q_) return;
    q_ = putblob(q_, coords, coords ? (uint32_t)(20) : NIL);
}
EXPORT void glDrawTexfOES(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height) {
    uint8_t *q_ = enc1(IDX1_glDrawTexfOES, 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = putf(q_, x);
    q_ = putf(q_, y);
    q_ = putf(q_, z);
    q_ = putf(q_, width);
    q_ = putf(q_, height);
}
EXPORT void glDrawTexfvOES(const GLfloat * coords) {
    uint8_t *q_ = enc1(IDX1_glDrawTexfvOES, BLOB1(coords ? (uint32_t)(20) : NIL));
    if (!q_) return;
    q_ = putblob(q_, coords, coords ? (uint32_t)(20) : NIL);
}
EXPORT GLboolean glIsRenderbufferOES(GLuint renderbuffer) {
    uint8_t *q_ = enc1(IDX1_glIsRenderbufferOES, 4);
    if (!q_) return (GLboolean)0;
    q_ = put32(q_, (uint32_t)renderbuffer);
    if (!call1()) return (GLboolean)0;
    return (GLboolean)get1_32();
}
EXPORT void glBindRenderbufferOES(GLenum target, GLuint renderbuffer) {
    uint8_t *q_ = enc1(IDX1_glBindRenderbufferOES, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)renderbuffer);
}
EXPORT void glDeleteRenderbuffersOES(GLsizei n, const GLuint * renderbuffers) {
    uint8_t *q_ = enc1(IDX1_glDeleteRenderbuffersOES, 4 + BLOB1(renderbuffers ? (uint32_t)(n * 4) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)n);
    q_ = putblob(q_, renderbuffers, renderbuffers ? (uint32_t)(n * 4) : NIL);
}
EXPORT void glGenRenderbuffersOES(GLsizei n, GLuint * renderbuffers) {
    uint8_t *q_ = enc1(IDX1_glGenRenderbuffersOES, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)n);
    if (!call1()) return;
    get1(renderbuffers, (uint32_t)(n * 4));
}
EXPORT void glRenderbufferStorageOES(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {
    uint8_t *q_ = enc1(IDX1_glRenderbufferStorageOES, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)internalformat);
    q_ = put32(q_, (uint32_t)width);
    q_ = put32(q_, (uint32_t)height);
}
EXPORT void glGetRenderbufferParameterivOES(GLenum target, GLenum pname, GLint * params) {
    uint8_t *q_ = enc1(IDX1_glGetRenderbufferParameterivOES, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4));
}
EXPORT GLboolean glIsFramebufferOES(GLuint framebuffer) {
    uint8_t *q_ = enc1(IDX1_glIsFramebufferOES, 4);
    if (!q_) return (GLboolean)0;
    q_ = put32(q_, (uint32_t)framebuffer);
    if (!call1()) return (GLboolean)0;
    return (GLboolean)get1_32();
}
EXPORT void glBindFramebufferOES(GLenum target, GLuint framebuffer) {
    uint8_t *q_ = enc1(IDX1_glBindFramebufferOES, 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)framebuffer);
}
EXPORT void glDeleteFramebuffersOES(GLsizei n, const GLuint * framebuffers) {
    uint8_t *q_ = enc1(IDX1_glDeleteFramebuffersOES, 4 + BLOB1(framebuffers ? (uint32_t)(n * 4) : NIL));
    if (!q_) return;
    q_ = put32(q_, (uint32_t)n);
    q_ = putblob(q_, framebuffers, framebuffers ? (uint32_t)(n * 4) : NIL);
}
EXPORT void glGenFramebuffersOES(GLsizei n, GLuint * framebuffers) {
    uint8_t *q_ = enc1(IDX1_glGenFramebuffersOES, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)n);
    if (!call1()) return;
    get1(framebuffers, (uint32_t)(n * 4));
}
EXPORT GLenum glCheckFramebufferStatusOES(GLenum target) {
    uint8_t *q_ = enc1(IDX1_glCheckFramebufferStatusOES, 4);
    if (!q_) return (GLenum)0;
    q_ = put32(q_, (uint32_t)target);
    if (!call1()) return (GLenum)0;
    return (GLenum)get1_32();
}
EXPORT void glFramebufferRenderbufferOES(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    uint8_t *q_ = enc1(IDX1_glFramebufferRenderbufferOES, 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)attachment);
    q_ = put32(q_, (uint32_t)renderbuffertarget);
    q_ = put32(q_, (uint32_t)renderbuffer);
}
EXPORT void glFramebufferTexture2DOES(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    uint8_t *q_ = enc1(IDX1_glFramebufferTexture2DOES, 4 + 4 + 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)attachment);
    q_ = put32(q_, (uint32_t)textarget);
    q_ = put32(q_, (uint32_t)texture);
    q_ = put32(q_, (uint32_t)level);
}
EXPORT void glGetFramebufferAttachmentParameterivOES(GLenum target, GLenum attachment, GLenum pname, GLint * params) {
    uint8_t *q_ = enc1(IDX1_glGetFramebufferAttachmentParameterivOES, 4 + 4 + 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
    q_ = put32(q_, (uint32_t)attachment);
    q_ = put32(q_, (uint32_t)pname);
    if (!call1()) return;
    get1(params, (uint32_t)(4));
}
EXPORT void glGenerateMipmapOES(GLenum target) {
    uint8_t *q_ = enc1(IDX1_glGenerateMipmapOES, 4);
    if (!q_) return;
    q_ = put32(q_, (uint32_t)target);
}
