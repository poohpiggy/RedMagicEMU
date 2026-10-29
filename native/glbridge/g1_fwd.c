/* written by gen1.py */
#include "fwd1.h"
EXPORT void glAlphaFunc(GLenum func, GLfloat ref) { FWD(glAlphaFunc, (void (*)(GLenum, GLfloat)))(func, ref); }
EXPORT void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { FWD(glClearColor, (void (*)(GLfloat, GLfloat, GLfloat, GLfloat)))(red, green, blue, alpha); }
EXPORT void glClearDepthf(GLfloat d) { FWD(glClearDepthf, (void (*)(GLfloat)))(d); }
EXPORT void glClipPlanef(GLenum p, const GLfloat * eqn) { FWD(glClipPlanef, (void (*)(GLenum, const GLfloat *)))(p, eqn); }
EXPORT void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { FWD(glColor4f, (void (*)(GLfloat, GLfloat, GLfloat, GLfloat)))(red, green, blue, alpha); }
EXPORT void glDepthRangef(GLfloat n, GLfloat f) { FWD(glDepthRangef, (void (*)(GLfloat, GLfloat)))(n, f); }
EXPORT void glFogf(GLenum pname, GLfloat param) { FWD(glFogf, (void (*)(GLenum, GLfloat)))(pname, param); }
EXPORT void glFogfv(GLenum pname, const GLfloat * params) { FWD(glFogfv, (void (*)(GLenum, const GLfloat *)))(pname, params); }
EXPORT void glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { FWD(glFrustumf, (void (*)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)))(l, r, b, t, n, f); }
EXPORT void glGetClipPlanef(GLenum plane, GLfloat * equation) { FWD(glGetClipPlanef, (void (*)(GLenum, GLfloat *)))(plane, equation); }
EXPORT void glGetFloatv(GLenum pname, GLfloat * data) { FWD(glGetFloatv, (void (*)(GLenum, GLfloat *)))(pname, data); }
EXPORT void glGetLightfv(GLenum light, GLenum pname, GLfloat * params) { FWD(glGetLightfv, (void (*)(GLenum, GLenum, GLfloat *)))(light, pname, params); }
EXPORT void glGetMaterialfv(GLenum face, GLenum pname, GLfloat * params) { FWD(glGetMaterialfv, (void (*)(GLenum, GLenum, GLfloat *)))(face, pname, params); }
EXPORT void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat * params) { FWD(glGetTexEnvfv, (void (*)(GLenum, GLenum, GLfloat *)))(target, pname, params); }
EXPORT void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat * params) { FWD(glGetTexParameterfv, (void (*)(GLenum, GLenum, GLfloat *)))(target, pname, params); }
EXPORT void glLightModelf(GLenum pname, GLfloat param) { FWD(glLightModelf, (void (*)(GLenum, GLfloat)))(pname, param); }
EXPORT void glLightModelfv(GLenum pname, const GLfloat * params) { FWD(glLightModelfv, (void (*)(GLenum, const GLfloat *)))(pname, params); }
EXPORT void glLightf(GLenum light, GLenum pname, GLfloat param) { FWD(glLightf, (void (*)(GLenum, GLenum, GLfloat)))(light, pname, param); }
EXPORT void glLightfv(GLenum light, GLenum pname, const GLfloat * params) { FWD(glLightfv, (void (*)(GLenum, GLenum, const GLfloat *)))(light, pname, params); }
EXPORT void glLineWidth(GLfloat width) { FWD(glLineWidth, (void (*)(GLfloat)))(width); }
EXPORT void glLoadMatrixf(const GLfloat * m) { FWD(glLoadMatrixf, (void (*)(const GLfloat *)))(m); }
EXPORT void glMaterialf(GLenum face, GLenum pname, GLfloat param) { FWD(glMaterialf, (void (*)(GLenum, GLenum, GLfloat)))(face, pname, param); }
EXPORT void glMaterialfv(GLenum face, GLenum pname, const GLfloat * params) { FWD(glMaterialfv, (void (*)(GLenum, GLenum, const GLfloat *)))(face, pname, params); }
EXPORT void glMultMatrixf(const GLfloat * m) { FWD(glMultMatrixf, (void (*)(const GLfloat *)))(m); }
EXPORT void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { FWD(glMultiTexCoord4f, (void (*)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat)))(target, s, t, r, q); }
EXPORT void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) { FWD(glNormal3f, (void (*)(GLfloat, GLfloat, GLfloat)))(nx, ny, nz); }
EXPORT void glOrthof(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f) { FWD(glOrthof, (void (*)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)))(l, r, b, t, n, f); }
EXPORT void glPointParameterf(GLenum pname, GLfloat param) { FWD(glPointParameterf, (void (*)(GLenum, GLfloat)))(pname, param); }
EXPORT void glPointParameterfv(GLenum pname, const GLfloat * params) { FWD(glPointParameterfv, (void (*)(GLenum, const GLfloat *)))(pname, params); }
EXPORT void glPointSize(GLfloat size) { FWD(glPointSize, (void (*)(GLfloat)))(size); }
EXPORT void glPolygonOffset(GLfloat factor, GLfloat units) { FWD(glPolygonOffset, (void (*)(GLfloat, GLfloat)))(factor, units); }
EXPORT void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) { FWD(glRotatef, (void (*)(GLfloat, GLfloat, GLfloat, GLfloat)))(angle, x, y, z); }
EXPORT void glScalef(GLfloat x, GLfloat y, GLfloat z) { FWD(glScalef, (void (*)(GLfloat, GLfloat, GLfloat)))(x, y, z); }
EXPORT void glTexEnvf(GLenum target, GLenum pname, GLfloat param) { FWD(glTexEnvf, (void (*)(GLenum, GLenum, GLfloat)))(target, pname, param); }
EXPORT void glTexEnvfv(GLenum target, GLenum pname, const GLfloat * params) { FWD(glTexEnvfv, (void (*)(GLenum, GLenum, const GLfloat *)))(target, pname, params); }
EXPORT void glTexParameterf(GLenum target, GLenum pname, GLfloat param) { FWD(glTexParameterf, (void (*)(GLenum, GLenum, GLfloat)))(target, pname, param); }
EXPORT void glTexParameterfv(GLenum target, GLenum pname, const GLfloat * params) { FWD(glTexParameterfv, (void (*)(GLenum, GLenum, const GLfloat *)))(target, pname, params); }
EXPORT void glTranslatef(GLfloat x, GLfloat y, GLfloat z) { FWD(glTranslatef, (void (*)(GLfloat, GLfloat, GLfloat)))(x, y, z); }
EXPORT void glActiveTexture(GLenum texture) { FWD(glActiveTexture, (void (*)(GLenum)))(texture); }
EXPORT void glAlphaFuncx(GLenum func, GLfixed ref) { FWD(glAlphaFuncx, (void (*)(GLenum, GLfixed)))(func, ref); }
EXPORT void glBindBuffer(GLenum target, GLuint buffer) { FWD(glBindBuffer, (void (*)(GLenum, GLuint)))(target, buffer); }
EXPORT void glBindTexture(GLenum target, GLuint texture) { FWD(glBindTexture, (void (*)(GLenum, GLuint)))(target, texture); }
EXPORT void glBlendFunc(GLenum sfactor, GLenum dfactor) { FWD(glBlendFunc, (void (*)(GLenum, GLenum)))(sfactor, dfactor); }
EXPORT void glBufferData(GLenum target, GLsizeiptr size, const void * data, GLenum usage) { FWD(glBufferData, (void (*)(GLenum, GLsizeiptr, const void *, GLenum)))(target, size, data, usage); }
EXPORT void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void * data) { FWD(glBufferSubData, (void (*)(GLenum, GLintptr, GLsizeiptr, const void *)))(target, offset, size, data); }
EXPORT void glClear(GLbitfield mask) { FWD(glClear, (void (*)(GLbitfield)))(mask); }
EXPORT void glClearColorx(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) { FWD(glClearColorx, (void (*)(GLfixed, GLfixed, GLfixed, GLfixed)))(red, green, blue, alpha); }
EXPORT void glClearDepthx(GLfixed depth) { FWD(glClearDepthx, (void (*)(GLfixed)))(depth); }
EXPORT void glClearStencil(GLint s) { FWD(glClearStencil, (void (*)(GLint)))(s); }
EXPORT void glClientActiveTexture(GLenum texture) { FWD(glClientActiveTexture, (void (*)(GLenum)))(texture); }
EXPORT void glClipPlanex(GLenum plane, const GLfixed * equation) { FWD(glClipPlanex, (void (*)(GLenum, const GLfixed *)))(plane, equation); }
EXPORT void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha) { FWD(glColor4ub, (void (*)(GLubyte, GLubyte, GLubyte, GLubyte)))(red, green, blue, alpha); }
EXPORT void glColor4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) { FWD(glColor4x, (void (*)(GLfixed, GLfixed, GLfixed, GLfixed)))(red, green, blue, alpha); }
EXPORT void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { FWD(glColorMask, (void (*)(GLboolean, GLboolean, GLboolean, GLboolean)))(red, green, blue, alpha); }
EXPORT void glColorPointer(GLint size, GLenum type, GLsizei stride, const void * pointer) { FWD(glColorPointer, (void (*)(GLint, GLenum, GLsizei, const void *)))(size, type, stride, pointer); }
EXPORT void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void * data) { FWD(glCompressedTexImage2D, (void (*)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *)))(target, level, internalformat, width, height, border, imageSize, data); }
EXPORT void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void * data) { FWD(glCompressedTexSubImage2D, (void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *)))(target, level, xoffset, yoffset, width, height, format, imageSize, data); }
EXPORT void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) { FWD(glCopyTexImage2D, (void (*)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)))(target, level, internalformat, x, y, width, height, border); }
EXPORT void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height) { FWD(glCopyTexSubImage2D, (void (*)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)))(target, level, xoffset, yoffset, x, y, width, height); }
EXPORT void glCullFace(GLenum mode) { FWD(glCullFace, (void (*)(GLenum)))(mode); }
EXPORT void glDeleteBuffers(GLsizei n, const GLuint * buffers) { FWD(glDeleteBuffers, (void (*)(GLsizei, const GLuint *)))(n, buffers); }
EXPORT void glDeleteTextures(GLsizei n, const GLuint * textures) { FWD(glDeleteTextures, (void (*)(GLsizei, const GLuint *)))(n, textures); }
EXPORT void glDepthFunc(GLenum func) { FWD(glDepthFunc, (void (*)(GLenum)))(func); }
EXPORT void glDepthMask(GLboolean flag) { FWD(glDepthMask, (void (*)(GLboolean)))(flag); }
EXPORT void glDepthRangex(GLfixed n, GLfixed f) { FWD(glDepthRangex, (void (*)(GLfixed, GLfixed)))(n, f); }
EXPORT void glDisable(GLenum cap) { FWD(glDisable, (void (*)(GLenum)))(cap); }
EXPORT void glDisableClientState(GLenum array) { FWD(glDisableClientState, (void (*)(GLenum)))(array); }
EXPORT void glDrawArrays(GLenum mode, GLint first, GLsizei count) { FWD(glDrawArrays, (void (*)(GLenum, GLint, GLsizei)))(mode, first, count); }
EXPORT void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void * indices) { FWD(glDrawElements, (void (*)(GLenum, GLsizei, GLenum, const void *)))(mode, count, type, indices); }
EXPORT void glEnable(GLenum cap) { FWD(glEnable, (void (*)(GLenum)))(cap); }
EXPORT void glEnableClientState(GLenum array) { FWD(glEnableClientState, (void (*)(GLenum)))(array); }
EXPORT void glFinish(void) { FWD(glFinish, (void (*)(void)))(); }
EXPORT void glFlush(void) { FWD(glFlush, (void (*)(void)))(); }
EXPORT void glFogx(GLenum pname, GLfixed param) { FWD(glFogx, (void (*)(GLenum, GLfixed)))(pname, param); }
EXPORT void glFogxv(GLenum pname, const GLfixed * param) { FWD(glFogxv, (void (*)(GLenum, const GLfixed *)))(pname, param); }
EXPORT void glFrontFace(GLenum mode) { FWD(glFrontFace, (void (*)(GLenum)))(mode); }
EXPORT void glFrustumx(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) { FWD(glFrustumx, (void (*)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed)))(l, r, b, t, n, f); }
EXPORT void glGetBooleanv(GLenum pname, GLboolean * data) { FWD(glGetBooleanv, (void (*)(GLenum, GLboolean *)))(pname, data); }
EXPORT void glGetBufferParameteriv(GLenum target, GLenum pname, GLint * params) { FWD(glGetBufferParameteriv, (void (*)(GLenum, GLenum, GLint *)))(target, pname, params); }
EXPORT void glGetClipPlanex(GLenum plane, GLfixed * equation) { FWD(glGetClipPlanex, (void (*)(GLenum, GLfixed *)))(plane, equation); }
EXPORT void glGenBuffers(GLsizei n, GLuint * buffers) { FWD(glGenBuffers, (void (*)(GLsizei, GLuint *)))(n, buffers); }
EXPORT void glGenTextures(GLsizei n, GLuint * textures) { FWD(glGenTextures, (void (*)(GLsizei, GLuint *)))(n, textures); }
EXPORT GLenum glGetError(void) { return FWD(glGetError, (GLenum (*)(void)))(); }
EXPORT void glGetFixedv(GLenum pname, GLfixed * params) { FWD(glGetFixedv, (void (*)(GLenum, GLfixed *)))(pname, params); }
EXPORT void glGetIntegerv(GLenum pname, GLint * data) { FWD(glGetIntegerv, (void (*)(GLenum, GLint *)))(pname, data); }
EXPORT void glGetLightxv(GLenum light, GLenum pname, GLfixed * params) { FWD(glGetLightxv, (void (*)(GLenum, GLenum, GLfixed *)))(light, pname, params); }
EXPORT void glGetMaterialxv(GLenum face, GLenum pname, GLfixed * params) { FWD(glGetMaterialxv, (void (*)(GLenum, GLenum, GLfixed *)))(face, pname, params); }
EXPORT void glGetPointerv(GLenum pname, void ** params) { FWD(glGetPointerv, (void (*)(GLenum, void **)))(pname, params); }
EXPORT const GLubyte * glGetString(GLenum name) { return FWD(glGetString, (const GLubyte * (*)(GLenum)))(name); }
EXPORT void glGetTexEnviv(GLenum target, GLenum pname, GLint * params) { FWD(glGetTexEnviv, (void (*)(GLenum, GLenum, GLint *)))(target, pname, params); }
EXPORT void glGetTexEnvxv(GLenum target, GLenum pname, GLfixed * params) { FWD(glGetTexEnvxv, (void (*)(GLenum, GLenum, GLfixed *)))(target, pname, params); }
EXPORT void glGetTexParameteriv(GLenum target, GLenum pname, GLint * params) { FWD(glGetTexParameteriv, (void (*)(GLenum, GLenum, GLint *)))(target, pname, params); }
EXPORT void glGetTexParameterxv(GLenum target, GLenum pname, GLfixed * params) { FWD(glGetTexParameterxv, (void (*)(GLenum, GLenum, GLfixed *)))(target, pname, params); }
EXPORT void glHint(GLenum target, GLenum mode) { FWD(glHint, (void (*)(GLenum, GLenum)))(target, mode); }
EXPORT GLboolean glIsBuffer(GLuint buffer) { return FWD(glIsBuffer, (GLboolean (*)(GLuint)))(buffer); }
EXPORT GLboolean glIsEnabled(GLenum cap) { return FWD(glIsEnabled, (GLboolean (*)(GLenum)))(cap); }
EXPORT GLboolean glIsTexture(GLuint texture) { return FWD(glIsTexture, (GLboolean (*)(GLuint)))(texture); }
EXPORT void glLightModelx(GLenum pname, GLfixed param) { FWD(glLightModelx, (void (*)(GLenum, GLfixed)))(pname, param); }
EXPORT void glLightModelxv(GLenum pname, const GLfixed * param) { FWD(glLightModelxv, (void (*)(GLenum, const GLfixed *)))(pname, param); }
EXPORT void glLightx(GLenum light, GLenum pname, GLfixed param) { FWD(glLightx, (void (*)(GLenum, GLenum, GLfixed)))(light, pname, param); }
EXPORT void glLightxv(GLenum light, GLenum pname, const GLfixed * params) { FWD(glLightxv, (void (*)(GLenum, GLenum, const GLfixed *)))(light, pname, params); }
EXPORT void glLineWidthx(GLfixed width) { FWD(glLineWidthx, (void (*)(GLfixed)))(width); }
EXPORT void glLoadIdentity(void) { FWD(glLoadIdentity, (void (*)(void)))(); }
EXPORT void glLoadMatrixx(const GLfixed * m) { FWD(glLoadMatrixx, (void (*)(const GLfixed *)))(m); }
EXPORT void glLogicOp(GLenum opcode) { FWD(glLogicOp, (void (*)(GLenum)))(opcode); }
EXPORT void glMaterialx(GLenum face, GLenum pname, GLfixed param) { FWD(glMaterialx, (void (*)(GLenum, GLenum, GLfixed)))(face, pname, param); }
EXPORT void glMaterialxv(GLenum face, GLenum pname, const GLfixed * param) { FWD(glMaterialxv, (void (*)(GLenum, GLenum, const GLfixed *)))(face, pname, param); }
EXPORT void glMatrixMode(GLenum mode) { FWD(glMatrixMode, (void (*)(GLenum)))(mode); }
EXPORT void glMultMatrixx(const GLfixed * m) { FWD(glMultMatrixx, (void (*)(const GLfixed *)))(m); }
EXPORT void glMultiTexCoord4x(GLenum texture, GLfixed s, GLfixed t, GLfixed r, GLfixed q) { FWD(glMultiTexCoord4x, (void (*)(GLenum, GLfixed, GLfixed, GLfixed, GLfixed)))(texture, s, t, r, q); }
EXPORT void glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz) { FWD(glNormal3x, (void (*)(GLfixed, GLfixed, GLfixed)))(nx, ny, nz); }
EXPORT void glNormalPointer(GLenum type, GLsizei stride, const void * pointer) { FWD(glNormalPointer, (void (*)(GLenum, GLsizei, const void *)))(type, stride, pointer); }
EXPORT void glOrthox(GLfixed l, GLfixed r, GLfixed b, GLfixed t, GLfixed n, GLfixed f) { FWD(glOrthox, (void (*)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed)))(l, r, b, t, n, f); }
EXPORT void glPixelStorei(GLenum pname, GLint param) { FWD(glPixelStorei, (void (*)(GLenum, GLint)))(pname, param); }
EXPORT void glPointParameterx(GLenum pname, GLfixed param) { FWD(glPointParameterx, (void (*)(GLenum, GLfixed)))(pname, param); }
EXPORT void glPointParameterxv(GLenum pname, const GLfixed * params) { FWD(glPointParameterxv, (void (*)(GLenum, const GLfixed *)))(pname, params); }
EXPORT void glPointSizex(GLfixed size) { FWD(glPointSizex, (void (*)(GLfixed)))(size); }
EXPORT void glPolygonOffsetx(GLfixed factor, GLfixed units) { FWD(glPolygonOffsetx, (void (*)(GLfixed, GLfixed)))(factor, units); }
EXPORT void glPopMatrix(void) { FWD(glPopMatrix, (void (*)(void)))(); }
EXPORT void glPushMatrix(void) { FWD(glPushMatrix, (void (*)(void)))(); }
EXPORT void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void * pixels) { FWD(glReadPixels, (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)))(x, y, width, height, format, type, pixels); }
EXPORT void glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z) { FWD(glRotatex, (void (*)(GLfixed, GLfixed, GLfixed, GLfixed)))(angle, x, y, z); }
EXPORT void glSampleCoverage(GLfloat value, GLboolean invert) { FWD(glSampleCoverage, (void (*)(GLfloat, GLboolean)))(value, invert); }
EXPORT void glSampleCoveragex(GLclampx value, GLboolean invert) { FWD(glSampleCoveragex, (void (*)(GLclampx, GLboolean)))(value, invert); }
EXPORT void glScalex(GLfixed x, GLfixed y, GLfixed z) { FWD(glScalex, (void (*)(GLfixed, GLfixed, GLfixed)))(x, y, z); }
EXPORT void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) { FWD(glScissor, (void (*)(GLint, GLint, GLsizei, GLsizei)))(x, y, width, height); }
EXPORT void glShadeModel(GLenum mode) { FWD(glShadeModel, (void (*)(GLenum)))(mode); }
EXPORT void glStencilFunc(GLenum func, GLint ref, GLuint mask) { FWD(glStencilFunc, (void (*)(GLenum, GLint, GLuint)))(func, ref, mask); }
EXPORT void glStencilMask(GLuint mask) { FWD(glStencilMask, (void (*)(GLuint)))(mask); }
EXPORT void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) { FWD(glStencilOp, (void (*)(GLenum, GLenum, GLenum)))(fail, zfail, zpass); }
EXPORT void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void * pointer) { FWD(glTexCoordPointer, (void (*)(GLint, GLenum, GLsizei, const void *)))(size, type, stride, pointer); }
EXPORT void glTexEnvi(GLenum target, GLenum pname, GLint param) { FWD(glTexEnvi, (void (*)(GLenum, GLenum, GLint)))(target, pname, param); }
EXPORT void glTexEnvx(GLenum target, GLenum pname, GLfixed param) { FWD(glTexEnvx, (void (*)(GLenum, GLenum, GLfixed)))(target, pname, param); }
EXPORT void glTexEnviv(GLenum target, GLenum pname, const GLint * params) { FWD(glTexEnviv, (void (*)(GLenum, GLenum, const GLint *)))(target, pname, params); }
EXPORT void glTexEnvxv(GLenum target, GLenum pname, const GLfixed * params) { FWD(glTexEnvxv, (void (*)(GLenum, GLenum, const GLfixed *)))(target, pname, params); }
EXPORT void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void * pixels) { FWD(glTexImage2D, (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)))(target, level, internalformat, width, height, border, format, type, pixels); }
EXPORT void glTexParameteri(GLenum target, GLenum pname, GLint param) { FWD(glTexParameteri, (void (*)(GLenum, GLenum, GLint)))(target, pname, param); }
EXPORT void glTexParameterx(GLenum target, GLenum pname, GLfixed param) { FWD(glTexParameterx, (void (*)(GLenum, GLenum, GLfixed)))(target, pname, param); }
EXPORT void glTexParameteriv(GLenum target, GLenum pname, const GLint * params) { FWD(glTexParameteriv, (void (*)(GLenum, GLenum, const GLint *)))(target, pname, params); }
EXPORT void glTexParameterxv(GLenum target, GLenum pname, const GLfixed * params) { FWD(glTexParameterxv, (void (*)(GLenum, GLenum, const GLfixed *)))(target, pname, params); }
EXPORT void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void * pixels) { FWD(glTexSubImage2D, (void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)))(target, level, xoffset, yoffset, width, height, format, type, pixels); }
EXPORT void glTranslatex(GLfixed x, GLfixed y, GLfixed z) { FWD(glTranslatex, (void (*)(GLfixed, GLfixed, GLfixed)))(x, y, z); }
EXPORT void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void * pointer) { FWD(glVertexPointer, (void (*)(GLint, GLenum, GLsizei, const void *)))(size, type, stride, pointer); }
EXPORT void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { FWD(glViewport, (void (*)(GLint, GLint, GLsizei, GLsizei)))(x, y, width, height); }
EXPORT void glPointSizePointerOES(GLenum type, GLsizei stride, const void * pointer) { FWD(glPointSizePointerOES, (void (*)(GLenum, GLsizei, const void *)))(type, stride, pointer); }
EXPORT void glBlendEquationSeparateOES(GLenum modeRGB, GLenum modeAlpha) { FWD(glBlendEquationSeparateOES, (void (*)(GLenum, GLenum)))(modeRGB, modeAlpha); }
EXPORT void glBlendFuncSeparateOES(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { FWD(glBlendFuncSeparateOES, (void (*)(GLenum, GLenum, GLenum, GLenum)))(srcRGB, dstRGB, srcAlpha, dstAlpha); }
EXPORT void glBlendEquationOES(GLenum mode) { FWD(glBlendEquationOES, (void (*)(GLenum)))(mode); }
EXPORT void glDrawTexsOES(GLshort x, GLshort y, GLshort z, GLshort width, GLshort height) { FWD(glDrawTexsOES, (void (*)(GLshort, GLshort, GLshort, GLshort, GLshort)))(x, y, z, width, height); }
EXPORT void glDrawTexiOES(GLint x, GLint y, GLint z, GLint width, GLint height) { FWD(glDrawTexiOES, (void (*)(GLint, GLint, GLint, GLint, GLint)))(x, y, z, width, height); }
EXPORT void glDrawTexxOES(GLfixed x, GLfixed y, GLfixed z, GLfixed width, GLfixed height) { FWD(glDrawTexxOES, (void (*)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed)))(x, y, z, width, height); }
EXPORT void glDrawTexsvOES(const GLshort * coords) { FWD(glDrawTexsvOES, (void (*)(const GLshort *)))(coords); }
EXPORT void glDrawTexivOES(const GLint * coords) { FWD(glDrawTexivOES, (void (*)(const GLint *)))(coords); }
EXPORT void glDrawTexxvOES(const GLfixed * coords) { FWD(glDrawTexxvOES, (void (*)(const GLfixed *)))(coords); }
EXPORT void glDrawTexfOES(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height) { FWD(glDrawTexfOES, (void (*)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)))(x, y, z, width, height); }
EXPORT void glDrawTexfvOES(const GLfloat * coords) { FWD(glDrawTexfvOES, (void (*)(const GLfloat *)))(coords); }
EXPORT GLboolean glIsRenderbufferOES(GLuint renderbuffer) { return FWD(glIsRenderbufferOES, (GLboolean (*)(GLuint)))(renderbuffer); }
EXPORT void glBindRenderbufferOES(GLenum target, GLuint renderbuffer) { FWD(glBindRenderbufferOES, (void (*)(GLenum, GLuint)))(target, renderbuffer); }
EXPORT void glDeleteRenderbuffersOES(GLsizei n, const GLuint * renderbuffers) { FWD(glDeleteRenderbuffersOES, (void (*)(GLsizei, const GLuint *)))(n, renderbuffers); }
EXPORT void glGenRenderbuffersOES(GLsizei n, GLuint * renderbuffers) { FWD(glGenRenderbuffersOES, (void (*)(GLsizei, GLuint *)))(n, renderbuffers); }
EXPORT void glRenderbufferStorageOES(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) { FWD(glRenderbufferStorageOES, (void (*)(GLenum, GLenum, GLsizei, GLsizei)))(target, internalformat, width, height); }
EXPORT void glGetRenderbufferParameterivOES(GLenum target, GLenum pname, GLint * params) { FWD(glGetRenderbufferParameterivOES, (void (*)(GLenum, GLenum, GLint *)))(target, pname, params); }
EXPORT GLboolean glIsFramebufferOES(GLuint framebuffer) { return FWD(glIsFramebufferOES, (GLboolean (*)(GLuint)))(framebuffer); }
EXPORT void glBindFramebufferOES(GLenum target, GLuint framebuffer) { FWD(glBindFramebufferOES, (void (*)(GLenum, GLuint)))(target, framebuffer); }
EXPORT void glDeleteFramebuffersOES(GLsizei n, const GLuint * framebuffers) { FWD(glDeleteFramebuffersOES, (void (*)(GLsizei, const GLuint *)))(n, framebuffers); }
EXPORT void glGenFramebuffersOES(GLsizei n, GLuint * framebuffers) { FWD(glGenFramebuffersOES, (void (*)(GLsizei, GLuint *)))(n, framebuffers); }
EXPORT GLenum glCheckFramebufferStatusOES(GLenum target) { return FWD(glCheckFramebufferStatusOES, (GLenum (*)(GLenum)))(target); }
EXPORT void glFramebufferRenderbufferOES(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) { FWD(glFramebufferRenderbufferOES, (void (*)(GLenum, GLenum, GLenum, GLuint)))(target, attachment, renderbuffertarget, renderbuffer); }
EXPORT void glFramebufferTexture2DOES(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { FWD(glFramebufferTexture2DOES, (void (*)(GLenum, GLenum, GLenum, GLuint, GLint)))(target, attachment, textarget, texture, level); }
EXPORT void glGetFramebufferAttachmentParameterivOES(GLenum target, GLenum attachment, GLenum pname, GLint * params) { FWD(glGetFramebufferAttachmentParameterivOES, (void (*)(GLenum, GLenum, GLenum, GLint *)))(target, attachment, pname, params); }
EXPORT void glGenerateMipmapOES(GLenum target) { FWD(glGenerateMipmapOES, (void (*)(GLenum)))(target); }
