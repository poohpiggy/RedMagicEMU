/* written by gen1.py */
static struct {
    void (*glAlphaFunc)(GLenum, GLfloat);
    void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glClearDepthf)(GLfloat);
    void (*glClipPlanef)(GLenum, const GLfloat *);
    void (*glColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glDepthRangef)(GLfloat, GLfloat);
    void (*glFogf)(GLenum, GLfloat);
    void (*glFogfv)(GLenum, const GLfloat *);
    void (*glFrustumf)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glGetClipPlanef)(GLenum, GLfloat *);
    void (*glGetFloatv)(GLenum, GLfloat *);
    void (*glGetLightfv)(GLenum, GLenum, GLfloat *);
    void (*glGetMaterialfv)(GLenum, GLenum, GLfloat *);
    void (*glGetTexEnvfv)(GLenum, GLenum, GLfloat *);
    void (*glGetTexParameterfv)(GLenum, GLenum, GLfloat *);
    void (*glLightModelf)(GLenum, GLfloat);
    void (*glLightModelfv)(GLenum, const GLfloat *);
    void (*glLightf)(GLenum, GLenum, GLfloat);
    void (*glLightfv)(GLenum, GLenum, const GLfloat *);
    void (*glLineWidth)(GLfloat);
    void (*glLoadMatrixf)(const GLfloat *);
    void (*glMaterialf)(GLenum, GLenum, GLfloat);
    void (*glMaterialfv)(GLenum, GLenum, const GLfloat *);
    void (*glMultMatrixf)(const GLfloat *);
    void (*glMultiTexCoord4f)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glNormal3f)(GLfloat, GLfloat, GLfloat);
    void (*glOrthof)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glPointParameterf)(GLenum, GLfloat);
    void (*glPointParameterfv)(GLenum, const GLfloat *);
    void (*glPointSize)(GLfloat);
    void (*glPolygonOffset)(GLfloat, GLfloat);
    void (*glRotatef)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glScalef)(GLfloat, GLfloat, GLfloat);
    void (*glTexEnvf)(GLenum, GLenum, GLfloat);
    void (*glTexEnvfv)(GLenum, GLenum, const GLfloat *);
    void (*glTexParameterf)(GLenum, GLenum, GLfloat);
    void (*glTexParameterfv)(GLenum, GLenum, const GLfloat *);
    void (*glTranslatef)(GLfloat, GLfloat, GLfloat);
    void (*glActiveTexture)(GLenum);
    void (*glAlphaFuncx)(GLenum, GLfixed);
    void (*glBindBuffer)(GLenum, GLuint);
    void (*glBindTexture)(GLenum, GLuint);
    void (*glBlendFunc)(GLenum, GLenum);
    void (*glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
    void (*glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void *);
    void (*glClear)(GLbitfield);
    void (*glClearColorx)(GLfixed, GLfixed, GLfixed, GLfixed);
    void (*glClearDepthx)(GLfixed);
    void (*glClearStencil)(GLint);
    void (*glClientActiveTexture)(GLenum);
    void (*glClipPlanex)(GLenum, const GLfixed *);
    void (*glColor4ub)(GLubyte, GLubyte, GLubyte, GLubyte);
    void (*glColor4x)(GLfixed, GLfixed, GLfixed, GLfixed);
    void (*glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (*glColorPointer)(GLint, GLenum, GLsizei, const void *);
    void (*glCompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
    void (*glCompressedTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *);
    void (*glCopyTexImage2D)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
    void (*glCopyTexSubImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);
    void (*glCullFace)(GLenum);
    void (*glDeleteBuffers)(GLsizei, const GLuint *);
    void (*glDeleteTextures)(GLsizei, const GLuint *);
    void (*glDepthFunc)(GLenum);
    void (*glDepthMask)(GLboolean);
    void (*glDepthRangex)(GLfixed, GLfixed);
    void (*glDisable)(GLenum);
    void (*glDisableClientState)(GLenum);
    void (*glDrawArrays)(GLenum, GLint, GLsizei);
    void (*glDrawElements)(GLenum, GLsizei, GLenum, const void *);
    void (*glEnable)(GLenum);
    void (*glEnableClientState)(GLenum);
    void (*glFinish)(void);
    void (*glFlush)(void);
    void (*glFogx)(GLenum, GLfixed);
    void (*glFogxv)(GLenum, const GLfixed *);
    void (*glFrontFace)(GLenum);
    void (*glFrustumx)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed);
    void (*glGetBooleanv)(GLenum, GLboolean *);
    void (*glGetBufferParameteriv)(GLenum, GLenum, GLint *);
    void (*glGetClipPlanex)(GLenum, GLfixed *);
    void (*glGenBuffers)(GLsizei, GLuint *);
    void (*glGenTextures)(GLsizei, GLuint *);
    GLenum (*glGetError)(void);
    void (*glGetFixedv)(GLenum, GLfixed *);
    void (*glGetIntegerv)(GLenum, GLint *);
    void (*glGetLightxv)(GLenum, GLenum, GLfixed *);
    void (*glGetMaterialxv)(GLenum, GLenum, GLfixed *);
    void (*glGetPointerv)(GLenum, void **);
    const GLubyte * (*glGetString)(GLenum);
    void (*glGetTexEnviv)(GLenum, GLenum, GLint *);
    void (*glGetTexEnvxv)(GLenum, GLenum, GLfixed *);
    void (*glGetTexParameteriv)(GLenum, GLenum, GLint *);
    void (*glGetTexParameterxv)(GLenum, GLenum, GLfixed *);
    void (*glHint)(GLenum, GLenum);
    GLboolean (*glIsBuffer)(GLuint);
    GLboolean (*glIsEnabled)(GLenum);
    GLboolean (*glIsTexture)(GLuint);
    void (*glLightModelx)(GLenum, GLfixed);
    void (*glLightModelxv)(GLenum, const GLfixed *);
    void (*glLightx)(GLenum, GLenum, GLfixed);
    void (*glLightxv)(GLenum, GLenum, const GLfixed *);
    void (*glLineWidthx)(GLfixed);
    void (*glLoadIdentity)(void);
    void (*glLoadMatrixx)(const GLfixed *);
    void (*glLogicOp)(GLenum);
    void (*glMaterialx)(GLenum, GLenum, GLfixed);
    void (*glMaterialxv)(GLenum, GLenum, const GLfixed *);
    void (*glMatrixMode)(GLenum);
    void (*glMultMatrixx)(const GLfixed *);
    void (*glMultiTexCoord4x)(GLenum, GLfixed, GLfixed, GLfixed, GLfixed);
    void (*glNormal3x)(GLfixed, GLfixed, GLfixed);
    void (*glNormalPointer)(GLenum, GLsizei, const void *);
    void (*glOrthox)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed, GLfixed);
    void (*glPixelStorei)(GLenum, GLint);
    void (*glPointParameterx)(GLenum, GLfixed);
    void (*glPointParameterxv)(GLenum, const GLfixed *);
    void (*glPointSizex)(GLfixed);
    void (*glPolygonOffsetx)(GLfixed, GLfixed);
    void (*glPopMatrix)(void);
    void (*glPushMatrix)(void);
    void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
    void (*glRotatex)(GLfixed, GLfixed, GLfixed, GLfixed);
    void (*glSampleCoverage)(GLfloat, GLboolean);
    void (*glSampleCoveragex)(GLclampx, GLboolean);
    void (*glScalex)(GLfixed, GLfixed, GLfixed);
    void (*glScissor)(GLint, GLint, GLsizei, GLsizei);
    void (*glShadeModel)(GLenum);
    void (*glStencilFunc)(GLenum, GLint, GLuint);
    void (*glStencilMask)(GLuint);
    void (*glStencilOp)(GLenum, GLenum, GLenum);
    void (*glTexCoordPointer)(GLint, GLenum, GLsizei, const void *);
    void (*glTexEnvi)(GLenum, GLenum, GLint);
    void (*glTexEnvx)(GLenum, GLenum, GLfixed);
    void (*glTexEnviv)(GLenum, GLenum, const GLint *);
    void (*glTexEnvxv)(GLenum, GLenum, const GLfixed *);
    void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    void (*glTexParameteri)(GLenum, GLenum, GLint);
    void (*glTexParameterx)(GLenum, GLenum, GLfixed);
    void (*glTexParameteriv)(GLenum, GLenum, const GLint *);
    void (*glTexParameterxv)(GLenum, GLenum, const GLfixed *);
    void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
    void (*glTranslatex)(GLfixed, GLfixed, GLfixed);
    void (*glVertexPointer)(GLint, GLenum, GLsizei, const void *);
    void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
    void (*glPointSizePointerOES)(GLenum, GLsizei, const void *);
    void (*glBlendEquationSeparateOES)(GLenum, GLenum);
    void (*glBlendFuncSeparateOES)(GLenum, GLenum, GLenum, GLenum);
    void (*glBlendEquationOES)(GLenum);
    void (*glDrawTexsOES)(GLshort, GLshort, GLshort, GLshort, GLshort);
    void (*glDrawTexiOES)(GLint, GLint, GLint, GLint, GLint);
    void (*glDrawTexxOES)(GLfixed, GLfixed, GLfixed, GLfixed, GLfixed);
    void (*glDrawTexsvOES)(const GLshort *);
    void (*glDrawTexivOES)(const GLint *);
    void (*glDrawTexxvOES)(const GLfixed *);
    void (*glDrawTexfOES)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*glDrawTexfvOES)(const GLfloat *);
    GLboolean (*glIsRenderbufferOES)(GLuint);
    void (*glBindRenderbufferOES)(GLenum, GLuint);
    void (*glDeleteRenderbuffersOES)(GLsizei, const GLuint *);
    void (*glGenRenderbuffersOES)(GLsizei, GLuint *);
    void (*glRenderbufferStorageOES)(GLenum, GLenum, GLsizei, GLsizei);
    void (*glGetRenderbufferParameterivOES)(GLenum, GLenum, GLint *);
    GLboolean (*glIsFramebufferOES)(GLuint);
    void (*glBindFramebufferOES)(GLenum, GLuint);
    void (*glDeleteFramebuffersOES)(GLsizei, const GLuint *);
    void (*glGenFramebuffersOES)(GLsizei, GLuint *);
    GLenum (*glCheckFramebufferStatusOES)(GLenum);
    void (*glFramebufferRenderbufferOES)(GLenum, GLenum, GLenum, GLuint);
    void (*glFramebufferTexture2DOES)(GLenum, GLenum, GLenum, GLuint, GLint);
    void (*glGetFramebufferAttachmentParameterivOES)(GLenum, GLenum, GLenum, GLint *);
    void (*glGenerateMipmapOES)(GLenum);
} F1;
static void h1_decode(uint32_t op, const uint8_t *p, uint32_t len) {
    (void)len;
    switch (op) {
    case IDX1_glAlphaFunc: { GLenum a0 = (GLenum)rd32(&p); GLfloat a1 = rdf(&p); F1.glAlphaFunc(a0, a1); break; }
    case IDX1_glClearColor: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); GLfloat a3 = rdf(&p); F1.glClearColor(a0, a1, a2, a3); break; }
    case IDX1_glClearDepthf: { GLfloat a0 = rdf(&p); F1.glClearDepthf(a0); break; }
    case IDX1_glClipPlanef: { GLenum a0 = (GLenum)rd32(&p); uint32_t n1; const GLfloat * a1 = (const GLfloat *)rdblob(&p, &n1); F1.glClipPlanef(a0, a1); break; }
    case IDX1_glColor4f: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); GLfloat a3 = rdf(&p); F1.glColor4f(a0, a1, a2, a3); break; }
    case IDX1_glDepthRangef: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); F1.glDepthRangef(a0, a1); break; }
    case IDX1_glFogf: { GLenum a0 = (GLenum)rd32(&p); GLfloat a1 = rdf(&p); F1.glFogf(a0, a1); break; }
    case IDX1_glFogfv: { GLenum a0 = (GLenum)rd32(&p); uint32_t n1; const GLfloat * a1 = (const GLfloat *)rdblob(&p, &n1); F1.glFogfv(a0, a1); break; }
    case IDX1_glFrustumf: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); GLfloat a3 = rdf(&p); GLfloat a4 = rdf(&p); GLfloat a5 = rdf(&p); F1.glFrustumf(a0, a1, a2, a3, a4, a5); break; }
    case IDX1_glGetClipPlanef: { GLenum a0 = (GLenum)rd32(&p); GLfloat * a1 = 0; uint32_t o1 = (uint32_t)(16); a1 = (GLfloat *)scratch1(o1); memset((void *)a1, 0, o1); F1.glGetClipPlanef(a0, a1); reply1((const void *)a1, o1); break; }
    case IDX1_glGetLightfv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfloat * a2 = 0; uint32_t o2 = (uint32_t)(4 * gl1_light_n(a1)); a2 = (GLfloat *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetLightfv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glGetMaterialfv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfloat * a2 = 0; uint32_t o2 = (uint32_t)(4 * gl1_material_n(a1)); a2 = (GLfloat *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetMaterialfv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glGetTexEnvfv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfloat * a2 = 0; uint32_t o2 = (uint32_t)(4 * (a1 == 0x2201 ? 4 : 1)); a2 = (GLfloat *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetTexEnvfv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glGetTexParameterfv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfloat * a2 = 0; uint32_t o2 = (uint32_t)(4 * (a1 == 0x8B9D ? 4 : 1)); a2 = (GLfloat *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetTexParameterfv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glLightModelf: { GLenum a0 = (GLenum)rd32(&p); GLfloat a1 = rdf(&p); F1.glLightModelf(a0, a1); break; }
    case IDX1_glLightModelfv: { GLenum a0 = (GLenum)rd32(&p); uint32_t n1; const GLfloat * a1 = (const GLfloat *)rdblob(&p, &n1); F1.glLightModelfv(a0, a1); break; }
    case IDX1_glLightf: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfloat a2 = rdf(&p); F1.glLightf(a0, a1, a2); break; }
    case IDX1_glLightfv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLfloat * a2 = (const GLfloat *)rdblob(&p, &n2); F1.glLightfv(a0, a1, a2); break; }
    case IDX1_glLineWidth: { GLfloat a0 = rdf(&p); F1.glLineWidth(a0); break; }
    case IDX1_glLoadMatrixf: { uint32_t n0; const GLfloat * a0 = (const GLfloat *)rdblob(&p, &n0); F1.glLoadMatrixf(a0); break; }
    case IDX1_glMaterialf: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfloat a2 = rdf(&p); F1.glMaterialf(a0, a1, a2); break; }
    case IDX1_glMaterialfv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLfloat * a2 = (const GLfloat *)rdblob(&p, &n2); F1.glMaterialfv(a0, a1, a2); break; }
    case IDX1_glMultMatrixf: { uint32_t n0; const GLfloat * a0 = (const GLfloat *)rdblob(&p, &n0); F1.glMultMatrixf(a0); break; }
    case IDX1_glMultiTexCoord4f: { GLenum a0 = (GLenum)rd32(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); GLfloat a3 = rdf(&p); GLfloat a4 = rdf(&p); F1.glMultiTexCoord4f(a0, a1, a2, a3, a4); break; }
    case IDX1_glNormal3f: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); F1.glNormal3f(a0, a1, a2); break; }
    case IDX1_glOrthof: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); GLfloat a3 = rdf(&p); GLfloat a4 = rdf(&p); GLfloat a5 = rdf(&p); F1.glOrthof(a0, a1, a2, a3, a4, a5); break; }
    case IDX1_glPointParameterf: { GLenum a0 = (GLenum)rd32(&p); GLfloat a1 = rdf(&p); F1.glPointParameterf(a0, a1); break; }
    case IDX1_glPointParameterfv: { GLenum a0 = (GLenum)rd32(&p); uint32_t n1; const GLfloat * a1 = (const GLfloat *)rdblob(&p, &n1); F1.glPointParameterfv(a0, a1); break; }
    case IDX1_glPointSize: { GLfloat a0 = rdf(&p); F1.glPointSize(a0); break; }
    case IDX1_glPolygonOffset: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); F1.glPolygonOffset(a0, a1); break; }
    case IDX1_glRotatef: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); GLfloat a3 = rdf(&p); F1.glRotatef(a0, a1, a2, a3); break; }
    case IDX1_glScalef: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); F1.glScalef(a0, a1, a2); break; }
    case IDX1_glTexEnvf: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfloat a2 = rdf(&p); F1.glTexEnvf(a0, a1, a2); break; }
    case IDX1_glTexEnvfv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLfloat * a2 = (const GLfloat *)rdblob(&p, &n2); F1.glTexEnvfv(a0, a1, a2); break; }
    case IDX1_glTexParameterf: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfloat a2 = rdf(&p); F1.glTexParameterf(a0, a1, a2); break; }
    case IDX1_glTexParameterfv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLfloat * a2 = (const GLfloat *)rdblob(&p, &n2); F1.glTexParameterfv(a0, a1, a2); break; }
    case IDX1_glTranslatef: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); F1.glTranslatef(a0, a1, a2); break; }
    case IDX1_glActiveTexture: { GLenum a0 = (GLenum)rd32(&p); F1.glActiveTexture(a0); break; }
    case IDX1_glAlphaFuncx: { GLenum a0 = (GLenum)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); F1.glAlphaFuncx(a0, a1); break; }
    case IDX1_glBindBuffer: { GLenum a0 = (GLenum)rd32(&p); GLuint a1 = (GLuint)rd32(&p); F1.glBindBuffer(a0, a1); break; }
    case IDX1_glBindTexture: { GLenum a0 = (GLenum)rd32(&p); GLuint a1 = (GLuint)rd32(&p); F1.glBindTexture(a0, a1); break; }
    case IDX1_glBlendFunc: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); F1.glBlendFunc(a0, a1); break; }
    case IDX1_glClear: { GLbitfield a0 = (GLbitfield)rd32(&p); F1.glClear(a0); break; }
    case IDX1_glClearColorx: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); GLfixed a3 = (GLfixed)rd32(&p); F1.glClearColorx(a0, a1, a2, a3); break; }
    case IDX1_glClearDepthx: { GLfixed a0 = (GLfixed)rd32(&p); F1.glClearDepthx(a0); break; }
    case IDX1_glClearStencil: { GLint a0 = (GLint)rd32(&p); F1.glClearStencil(a0); break; }
    case IDX1_glClipPlanex: { GLenum a0 = (GLenum)rd32(&p); uint32_t n1; const GLfixed * a1 = (const GLfixed *)rdblob(&p, &n1); F1.glClipPlanex(a0, a1); break; }
    case IDX1_glColor4ub: { GLubyte a0 = (GLubyte)rd32(&p); GLubyte a1 = (GLubyte)rd32(&p); GLubyte a2 = (GLubyte)rd32(&p); GLubyte a3 = (GLubyte)rd32(&p); F1.glColor4ub(a0, a1, a2, a3); break; }
    case IDX1_glColor4x: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); GLfixed a3 = (GLfixed)rd32(&p); F1.glColor4x(a0, a1, a2, a3); break; }
    case IDX1_glColorMask: { GLboolean a0 = (GLboolean)rd32(&p); GLboolean a1 = (GLboolean)rd32(&p); GLboolean a2 = (GLboolean)rd32(&p); GLboolean a3 = (GLboolean)rd32(&p); F1.glColorMask(a0, a1, a2, a3); break; }
    case IDX1_glCompressedTexImage2D: { GLenum a0 = (GLenum)rd32(&p); GLint a1 = (GLint)rd32(&p); GLenum a2 = (GLenum)rd32(&p); GLsizei a3 = (GLsizei)rd32(&p); GLsizei a4 = (GLsizei)rd32(&p); GLint a5 = (GLint)rd32(&p); GLsizei a6 = (GLsizei)rd32(&p); uint32_t n7; const void * a7 = (const void *)rdblob(&p, &n7); F1.glCompressedTexImage2D(a0, a1, a2, a3, a4, a5, a6, a7); break; }
    case IDX1_glCompressedTexSubImage2D: { GLenum a0 = (GLenum)rd32(&p); GLint a1 = (GLint)rd32(&p); GLint a2 = (GLint)rd32(&p); GLint a3 = (GLint)rd32(&p); GLsizei a4 = (GLsizei)rd32(&p); GLsizei a5 = (GLsizei)rd32(&p); GLenum a6 = (GLenum)rd32(&p); GLsizei a7 = (GLsizei)rd32(&p); uint32_t n8; const void * a8 = (const void *)rdblob(&p, &n8); F1.glCompressedTexSubImage2D(a0, a1, a2, a3, a4, a5, a6, a7, a8); break; }
    case IDX1_glCopyTexImage2D: { GLenum a0 = (GLenum)rd32(&p); GLint a1 = (GLint)rd32(&p); GLenum a2 = (GLenum)rd32(&p); GLint a3 = (GLint)rd32(&p); GLint a4 = (GLint)rd32(&p); GLsizei a5 = (GLsizei)rd32(&p); GLsizei a6 = (GLsizei)rd32(&p); GLint a7 = (GLint)rd32(&p); F1.glCopyTexImage2D(a0, a1, a2, a3, a4, a5, a6, a7); break; }
    case IDX1_glCopyTexSubImage2D: { GLenum a0 = (GLenum)rd32(&p); GLint a1 = (GLint)rd32(&p); GLint a2 = (GLint)rd32(&p); GLint a3 = (GLint)rd32(&p); GLint a4 = (GLint)rd32(&p); GLint a5 = (GLint)rd32(&p); GLsizei a6 = (GLsizei)rd32(&p); GLsizei a7 = (GLsizei)rd32(&p); F1.glCopyTexSubImage2D(a0, a1, a2, a3, a4, a5, a6, a7); break; }
    case IDX1_glCullFace: { GLenum a0 = (GLenum)rd32(&p); F1.glCullFace(a0); break; }
    case IDX1_glDeleteBuffers: { GLsizei a0 = (GLsizei)rd32(&p); uint32_t n1; const GLuint * a1 = (const GLuint *)rdblob(&p, &n1); F1.glDeleteBuffers(a0, a1); break; }
    case IDX1_glDeleteTextures: { GLsizei a0 = (GLsizei)rd32(&p); uint32_t n1; const GLuint * a1 = (const GLuint *)rdblob(&p, &n1); F1.glDeleteTextures(a0, a1); break; }
    case IDX1_glDepthFunc: { GLenum a0 = (GLenum)rd32(&p); F1.glDepthFunc(a0); break; }
    case IDX1_glDepthMask: { GLboolean a0 = (GLboolean)rd32(&p); F1.glDepthMask(a0); break; }
    case IDX1_glDepthRangex: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); F1.glDepthRangex(a0, a1); break; }
    case IDX1_glDisable: { GLenum a0 = (GLenum)rd32(&p); F1.glDisable(a0); break; }
    case IDX1_glEnable: { GLenum a0 = (GLenum)rd32(&p); F1.glEnable(a0); break; }
    case IDX1_glFlush: { F1.glFlush(); break; }
    case IDX1_glFogx: { GLenum a0 = (GLenum)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); F1.glFogx(a0, a1); break; }
    case IDX1_glFogxv: { GLenum a0 = (GLenum)rd32(&p); uint32_t n1; const GLfixed * a1 = (const GLfixed *)rdblob(&p, &n1); F1.glFogxv(a0, a1); break; }
    case IDX1_glFrontFace: { GLenum a0 = (GLenum)rd32(&p); F1.glFrontFace(a0); break; }
    case IDX1_glFrustumx: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); GLfixed a3 = (GLfixed)rd32(&p); GLfixed a4 = (GLfixed)rd32(&p); GLfixed a5 = (GLfixed)rd32(&p); F1.glFrustumx(a0, a1, a2, a3, a4, a5); break; }
    case IDX1_glGetBufferParameteriv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLint * a2 = 0; uint32_t o2 = (uint32_t)(4); a2 = (GLint *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetBufferParameteriv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glGetClipPlanex: { GLenum a0 = (GLenum)rd32(&p); GLfixed * a1 = 0; uint32_t o1 = (uint32_t)(16); a1 = (GLfixed *)scratch1(o1); memset((void *)a1, 0, o1); F1.glGetClipPlanex(a0, a1); reply1((const void *)a1, o1); break; }
    case IDX1_glGenBuffers: { GLsizei a0 = (GLsizei)rd32(&p); GLuint * a1 = 0; uint32_t o1 = (uint32_t)(a0 * 4); a1 = (GLuint *)scratch1(o1); memset((void *)a1, 0, o1); F1.glGenBuffers(a0, a1); reply1((const void *)a1, o1); break; }
    case IDX1_glGenTextures: { GLsizei a0 = (GLsizei)rd32(&p); GLuint * a1 = 0; uint32_t o1 = (uint32_t)(a0 * 4); a1 = (GLuint *)scratch1(o1); memset((void *)a1, 0, o1); F1.glGenTextures(a0, a1); reply1((const void *)a1, o1); break; }
    case IDX1_glGetError: { GLenum r = F1.glGetError(); reply1_32((uint32_t)r); break; }
    case IDX1_glGetLightxv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfixed * a2 = 0; uint32_t o2 = (uint32_t)(4 * gl1_light_n(a1)); a2 = (GLfixed *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetLightxv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glGetMaterialxv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfixed * a2 = 0; uint32_t o2 = (uint32_t)(4 * gl1_material_n(a1)); a2 = (GLfixed *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetMaterialxv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glGetTexEnviv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLint * a2 = 0; uint32_t o2 = (uint32_t)(4 * (a1 == 0x2201 ? 4 : 1)); a2 = (GLint *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetTexEnviv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glGetTexEnvxv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfixed * a2 = 0; uint32_t o2 = (uint32_t)(4 * (a1 == 0x2201 ? 4 : 1)); a2 = (GLfixed *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetTexEnvxv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glGetTexParameteriv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLint * a2 = 0; uint32_t o2 = (uint32_t)(4 * (a1 == 0x8B9D ? 4 : 1)); a2 = (GLint *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetTexParameteriv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glGetTexParameterxv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfixed * a2 = 0; uint32_t o2 = (uint32_t)(4 * (a1 == 0x8B9D ? 4 : 1)); a2 = (GLfixed *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetTexParameterxv(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glHint: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); F1.glHint(a0, a1); break; }
    case IDX1_glIsBuffer: { GLuint a0 = (GLuint)rd32(&p); GLboolean r = F1.glIsBuffer(a0); reply1_32((uint32_t)r); break; }
    case IDX1_glIsTexture: { GLuint a0 = (GLuint)rd32(&p); GLboolean r = F1.glIsTexture(a0); reply1_32((uint32_t)r); break; }
    case IDX1_glLightModelx: { GLenum a0 = (GLenum)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); F1.glLightModelx(a0, a1); break; }
    case IDX1_glLightModelxv: { GLenum a0 = (GLenum)rd32(&p); uint32_t n1; const GLfixed * a1 = (const GLfixed *)rdblob(&p, &n1); F1.glLightModelxv(a0, a1); break; }
    case IDX1_glLightx: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); F1.glLightx(a0, a1, a2); break; }
    case IDX1_glLightxv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLfixed * a2 = (const GLfixed *)rdblob(&p, &n2); F1.glLightxv(a0, a1, a2); break; }
    case IDX1_glLineWidthx: { GLfixed a0 = (GLfixed)rd32(&p); F1.glLineWidthx(a0); break; }
    case IDX1_glLoadIdentity: { F1.glLoadIdentity(); break; }
    case IDX1_glLoadMatrixx: { uint32_t n0; const GLfixed * a0 = (const GLfixed *)rdblob(&p, &n0); F1.glLoadMatrixx(a0); break; }
    case IDX1_glLogicOp: { GLenum a0 = (GLenum)rd32(&p); F1.glLogicOp(a0); break; }
    case IDX1_glMaterialx: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); F1.glMaterialx(a0, a1, a2); break; }
    case IDX1_glMaterialxv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLfixed * a2 = (const GLfixed *)rdblob(&p, &n2); F1.glMaterialxv(a0, a1, a2); break; }
    case IDX1_glMatrixMode: { GLenum a0 = (GLenum)rd32(&p); F1.glMatrixMode(a0); break; }
    case IDX1_glMultMatrixx: { uint32_t n0; const GLfixed * a0 = (const GLfixed *)rdblob(&p, &n0); F1.glMultMatrixx(a0); break; }
    case IDX1_glMultiTexCoord4x: { GLenum a0 = (GLenum)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); GLfixed a3 = (GLfixed)rd32(&p); GLfixed a4 = (GLfixed)rd32(&p); F1.glMultiTexCoord4x(a0, a1, a2, a3, a4); break; }
    case IDX1_glNormal3x: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); F1.glNormal3x(a0, a1, a2); break; }
    case IDX1_glOrthox: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); GLfixed a3 = (GLfixed)rd32(&p); GLfixed a4 = (GLfixed)rd32(&p); GLfixed a5 = (GLfixed)rd32(&p); F1.glOrthox(a0, a1, a2, a3, a4, a5); break; }
    case IDX1_glPixelStorei: { GLenum a0 = (GLenum)rd32(&p); GLint a1 = (GLint)rd32(&p); F1.glPixelStorei(a0, a1); break; }
    case IDX1_glPointParameterx: { GLenum a0 = (GLenum)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); F1.glPointParameterx(a0, a1); break; }
    case IDX1_glPointParameterxv: { GLenum a0 = (GLenum)rd32(&p); uint32_t n1; const GLfixed * a1 = (const GLfixed *)rdblob(&p, &n1); F1.glPointParameterxv(a0, a1); break; }
    case IDX1_glPointSizex: { GLfixed a0 = (GLfixed)rd32(&p); F1.glPointSizex(a0); break; }
    case IDX1_glPolygonOffsetx: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); F1.glPolygonOffsetx(a0, a1); break; }
    case IDX1_glPopMatrix: { F1.glPopMatrix(); break; }
    case IDX1_glPushMatrix: { F1.glPushMatrix(); break; }
    case IDX1_glRotatex: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); GLfixed a3 = (GLfixed)rd32(&p); F1.glRotatex(a0, a1, a2, a3); break; }
    case IDX1_glSampleCoverage: { GLfloat a0 = rdf(&p); GLboolean a1 = (GLboolean)rd32(&p); F1.glSampleCoverage(a0, a1); break; }
    case IDX1_glSampleCoveragex: { GLclampx a0 = (GLclampx)rd32(&p); GLboolean a1 = (GLboolean)rd32(&p); F1.glSampleCoveragex(a0, a1); break; }
    case IDX1_glScalex: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); F1.glScalex(a0, a1, a2); break; }
    case IDX1_glScissor: { GLint a0 = (GLint)rd32(&p); GLint a1 = (GLint)rd32(&p); GLsizei a2 = (GLsizei)rd32(&p); GLsizei a3 = (GLsizei)rd32(&p); F1.glScissor(a0, a1, a2, a3); break; }
    case IDX1_glShadeModel: { GLenum a0 = (GLenum)rd32(&p); F1.glShadeModel(a0); break; }
    case IDX1_glStencilFunc: { GLenum a0 = (GLenum)rd32(&p); GLint a1 = (GLint)rd32(&p); GLuint a2 = (GLuint)rd32(&p); F1.glStencilFunc(a0, a1, a2); break; }
    case IDX1_glStencilMask: { GLuint a0 = (GLuint)rd32(&p); F1.glStencilMask(a0); break; }
    case IDX1_glStencilOp: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLenum a2 = (GLenum)rd32(&p); F1.glStencilOp(a0, a1, a2); break; }
    case IDX1_glTexEnvi: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLint a2 = (GLint)rd32(&p); F1.glTexEnvi(a0, a1, a2); break; }
    case IDX1_glTexEnvx: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); F1.glTexEnvx(a0, a1, a2); break; }
    case IDX1_glTexEnviv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLint * a2 = (const GLint *)rdblob(&p, &n2); F1.glTexEnviv(a0, a1, a2); break; }
    case IDX1_glTexEnvxv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLfixed * a2 = (const GLfixed *)rdblob(&p, &n2); F1.glTexEnvxv(a0, a1, a2); break; }
    case IDX1_glTexParameteri: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLint a2 = (GLint)rd32(&p); F1.glTexParameteri(a0, a1, a2); break; }
    case IDX1_glTexParameterx: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); F1.glTexParameterx(a0, a1, a2); break; }
    case IDX1_glTexParameteriv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLint * a2 = (const GLint *)rdblob(&p, &n2); F1.glTexParameteriv(a0, a1, a2); break; }
    case IDX1_glTexParameterxv: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); uint32_t n2; const GLfixed * a2 = (const GLfixed *)rdblob(&p, &n2); F1.glTexParameterxv(a0, a1, a2); break; }
    case IDX1_glTranslatex: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); F1.glTranslatex(a0, a1, a2); break; }
    case IDX1_glViewport: { GLint a0 = (GLint)rd32(&p); GLint a1 = (GLint)rd32(&p); GLsizei a2 = (GLsizei)rd32(&p); GLsizei a3 = (GLsizei)rd32(&p); F1.glViewport(a0, a1, a2, a3); break; }
    case IDX1_glBlendEquationSeparateOES: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); F1.glBlendEquationSeparateOES(a0, a1); break; }
    case IDX1_glBlendFuncSeparateOES: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLenum a2 = (GLenum)rd32(&p); GLenum a3 = (GLenum)rd32(&p); F1.glBlendFuncSeparateOES(a0, a1, a2, a3); break; }
    case IDX1_glBlendEquationOES: { GLenum a0 = (GLenum)rd32(&p); F1.glBlendEquationOES(a0); break; }
    case IDX1_glDrawTexsOES: { GLshort a0 = (GLshort)rd32(&p); GLshort a1 = (GLshort)rd32(&p); GLshort a2 = (GLshort)rd32(&p); GLshort a3 = (GLshort)rd32(&p); GLshort a4 = (GLshort)rd32(&p); F1.glDrawTexsOES(a0, a1, a2, a3, a4); break; }
    case IDX1_glDrawTexiOES: { GLint a0 = (GLint)rd32(&p); GLint a1 = (GLint)rd32(&p); GLint a2 = (GLint)rd32(&p); GLint a3 = (GLint)rd32(&p); GLint a4 = (GLint)rd32(&p); F1.glDrawTexiOES(a0, a1, a2, a3, a4); break; }
    case IDX1_glDrawTexxOES: { GLfixed a0 = (GLfixed)rd32(&p); GLfixed a1 = (GLfixed)rd32(&p); GLfixed a2 = (GLfixed)rd32(&p); GLfixed a3 = (GLfixed)rd32(&p); GLfixed a4 = (GLfixed)rd32(&p); F1.glDrawTexxOES(a0, a1, a2, a3, a4); break; }
    case IDX1_glDrawTexsvOES: { uint32_t n0; const GLshort * a0 = (const GLshort *)rdblob(&p, &n0); F1.glDrawTexsvOES(a0); break; }
    case IDX1_glDrawTexivOES: { uint32_t n0; const GLint * a0 = (const GLint *)rdblob(&p, &n0); F1.glDrawTexivOES(a0); break; }
    case IDX1_glDrawTexxvOES: { uint32_t n0; const GLfixed * a0 = (const GLfixed *)rdblob(&p, &n0); F1.glDrawTexxvOES(a0); break; }
    case IDX1_glDrawTexfOES: { GLfloat a0 = rdf(&p); GLfloat a1 = rdf(&p); GLfloat a2 = rdf(&p); GLfloat a3 = rdf(&p); GLfloat a4 = rdf(&p); F1.glDrawTexfOES(a0, a1, a2, a3, a4); break; }
    case IDX1_glDrawTexfvOES: { uint32_t n0; const GLfloat * a0 = (const GLfloat *)rdblob(&p, &n0); F1.glDrawTexfvOES(a0); break; }
    case IDX1_glIsRenderbufferOES: { GLuint a0 = (GLuint)rd32(&p); GLboolean r = F1.glIsRenderbufferOES(a0); reply1_32((uint32_t)r); break; }
    case IDX1_glBindRenderbufferOES: { GLenum a0 = (GLenum)rd32(&p); GLuint a1 = (GLuint)rd32(&p); F1.glBindRenderbufferOES(a0, a1); break; }
    case IDX1_glDeleteRenderbuffersOES: { GLsizei a0 = (GLsizei)rd32(&p); uint32_t n1; const GLuint * a1 = (const GLuint *)rdblob(&p, &n1); F1.glDeleteRenderbuffersOES(a0, a1); break; }
    case IDX1_glGenRenderbuffersOES: { GLsizei a0 = (GLsizei)rd32(&p); GLuint * a1 = 0; uint32_t o1 = (uint32_t)(a0 * 4); a1 = (GLuint *)scratch1(o1); memset((void *)a1, 0, o1); F1.glGenRenderbuffersOES(a0, a1); reply1((const void *)a1, o1); break; }
    case IDX1_glRenderbufferStorageOES: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLsizei a2 = (GLsizei)rd32(&p); GLsizei a3 = (GLsizei)rd32(&p); F1.glRenderbufferStorageOES(a0, a1, a2, a3); break; }
    case IDX1_glGetRenderbufferParameterivOES: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLint * a2 = 0; uint32_t o2 = (uint32_t)(4); a2 = (GLint *)scratch1(o2); memset((void *)a2, 0, o2); F1.glGetRenderbufferParameterivOES(a0, a1, a2); reply1((const void *)a2, o2); break; }
    case IDX1_glIsFramebufferOES: { GLuint a0 = (GLuint)rd32(&p); GLboolean r = F1.glIsFramebufferOES(a0); reply1_32((uint32_t)r); break; }
    case IDX1_glBindFramebufferOES: { GLenum a0 = (GLenum)rd32(&p); GLuint a1 = (GLuint)rd32(&p); F1.glBindFramebufferOES(a0, a1); break; }
    case IDX1_glDeleteFramebuffersOES: { GLsizei a0 = (GLsizei)rd32(&p); uint32_t n1; const GLuint * a1 = (const GLuint *)rdblob(&p, &n1); F1.glDeleteFramebuffersOES(a0, a1); break; }
    case IDX1_glGenFramebuffersOES: { GLsizei a0 = (GLsizei)rd32(&p); GLuint * a1 = 0; uint32_t o1 = (uint32_t)(a0 * 4); a1 = (GLuint *)scratch1(o1); memset((void *)a1, 0, o1); F1.glGenFramebuffersOES(a0, a1); reply1((const void *)a1, o1); break; }
    case IDX1_glCheckFramebufferStatusOES: { GLenum a0 = (GLenum)rd32(&p); GLenum r = F1.glCheckFramebufferStatusOES(a0); reply1_32((uint32_t)r); break; }
    case IDX1_glFramebufferRenderbufferOES: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLenum a2 = (GLenum)rd32(&p); GLuint a3 = (GLuint)rd32(&p); F1.glFramebufferRenderbufferOES(a0, a1, a2, a3); break; }
    case IDX1_glFramebufferTexture2DOES: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLenum a2 = (GLenum)rd32(&p); GLuint a3 = (GLuint)rd32(&p); GLint a4 = (GLint)rd32(&p); F1.glFramebufferTexture2DOES(a0, a1, a2, a3, a4); break; }
    case IDX1_glGetFramebufferAttachmentParameterivOES: { GLenum a0 = (GLenum)rd32(&p); GLenum a1 = (GLenum)rd32(&p); GLenum a2 = (GLenum)rd32(&p); GLint * a3 = 0; uint32_t o3 = (uint32_t)(4); a3 = (GLint *)scratch1(o3); memset((void *)a3, 0, o3); F1.glGetFramebufferAttachmentParameterivOES(a0, a1, a2, a3); reply1((const void *)a3, o3); break; }
    case IDX1_glGenerateMipmapOES: { GLenum a0 = (GLenum)rd32(&p); F1.glGenerateMipmapOES(a0); break; }
    default: h1_hand(op, p, len); break;
    }
}
