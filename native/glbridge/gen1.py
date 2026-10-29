#!/usr/bin/env python3
"""The GLES 1 bridge for Android 1.x (guest1.c, host1.c): python gen1.py <ndk sysroot>/usr/include

gl1_api.h (both sides): the index of every bridged GLES 1.1 entry point and its name.
g1_enc.h (guest, libhEGL.so): every entry point that is not written by hand as an encoder into the command stream.
g1_fwd.c (guest, libhGLESv1_CM.so): every entry point as a call of the same one in libhEGL.so (libEGL of 1.x loads
    the two libraries apart; the state lives in one).
h1_dec.h (host): the decoder of the stream.

A call: uint32 index, uint32 payload length, the payload - scalars as 4 bytes (GLES 1 has no 64-bit ones on a
32-bit guest), a block as uint32 length (0xffffffff: a null pointer) and its bytes padded to 4. A call that returns
something or has "out" blocks is answered at once: the out blocks in order (each of the size both sides compute),
then the return value as 4 bytes."""
import re, sys

INC = sys.argv[1]


def parse(path):
    out = []
    for m in re.finditer(r'GL_API(?:CALL)?\s+(.+?)\s*GL_APIENTRY\s+(\w+)\s*\((.*?)\);', open(INC + '/' + path).read(), re.S):
        ret, name, args = m.group(1).strip(), m.group(2), ' '.join(m.group(3).split())
        params = []
        if args != 'void':
            for a in args.split(','):
                mm = re.match(r'(.*?)(\w+)$', a.strip())
                params.append((mm.group(1).strip(), mm.group(2)))
        out.append((name, ret, params))
    return out


core = parse('GLES/gl.h')
EXT = '''glBlendEquationOES glBlendFuncSeparateOES glBlendEquationSeparateOES glDrawTexsOES glDrawTexiOES glDrawTexxOES
glDrawTexsvOES glDrawTexivOES glDrawTexxvOES glDrawTexfOES glDrawTexfvOES glIsRenderbufferOES glBindRenderbufferOES
glDeleteRenderbuffersOES glGenRenderbuffersOES glRenderbufferStorageOES glGetRenderbufferParameterivOES
glIsFramebufferOES glBindFramebufferOES glDeleteFramebuffersOES glGenFramebuffersOES glCheckFramebufferStatusOES
glFramebufferRenderbufferOES glFramebufferTexture2DOES glGetFramebufferAttachmentParameterivOES glGenerateMipmapOES'''.split()
ext = [f for f in parse('GLES/glext.h') if f[0] in EXT]
funcs = core + ext
assert len(ext) == len(EXT), set(EXT) - {f[0] for f in ext}

# pointers: ('in' | 'out', size in bytes as a C expression of the parameters)
P4 = lambda n: "4 * " + n
SPEC = {
    'glLoadMatrixf': {'m': ('in', '64')}, 'glLoadMatrixx': {'m': ('in', '64')},
    'glMultMatrixf': {'m': ('in', '64')}, 'glMultMatrixx': {'m': ('in', '64')},
    'glClipPlanef': {'equation': ('in', '16')}, 'glClipPlanex': {'equation': ('in', '16')},
    'glGetClipPlanef': {'equation': ('out', '16')}, 'glGetClipPlanex': {'equation': ('out', '16')},
    'glLightfv': {'params': ('in', P4('gl1_light_n(pname)'))}, 'glLightxv': {'params': ('in', P4('gl1_light_n(pname)'))},
    'glGetLightfv': {'params': ('out', P4('gl1_light_n(pname)'))}, 'glGetLightxv': {'params': ('out', P4('gl1_light_n(pname)'))},
    'glMaterialfv': {'params': ('in', P4('gl1_material_n(pname)'))}, 'glMaterialxv': {'params': ('in', P4('gl1_material_n(pname)'))},
    'glGetMaterialfv': {'params': ('out', P4('gl1_material_n(pname)'))}, 'glGetMaterialxv': {'params': ('out', P4('gl1_material_n(pname)'))},
    'glLightModelfv': {'params': ('in', P4('(pname == 0x0B53 ? 4 : 1)'))}, 'glLightModelxv': {'params': ('in', P4('(pname == 0x0B53 ? 4 : 1)'))},
    'glFogfv': {'params': ('in', P4('(pname == 0x0B66 ? 4 : 1)'))}, 'glFogxv': {'params': ('in', P4('(pname == 0x0B66 ? 4 : 1)'))},
    'glTexEnvfv': {'params': ('in', P4('(pname == 0x2201 ? 4 : 1)'))}, 'glTexEnviv': {'params': ('in', P4('(pname == 0x2201 ? 4 : 1)'))},
    'glTexEnvxv': {'params': ('in', P4('(pname == 0x2201 ? 4 : 1)'))},
    'glGetTexEnvfv': {'params': ('out', P4('(pname == 0x2201 ? 4 : 1)'))}, 'glGetTexEnviv': {'params': ('out', P4('(pname == 0x2201 ? 4 : 1)'))},
    'glGetTexEnvxv': {'params': ('out', P4('(pname == 0x2201 ? 4 : 1)'))},
    'glTexParameterfv': {'params': ('in', P4('(pname == 0x8B9D ? 4 : 1)'))}, 'glTexParameteriv': {'params': ('in', P4('(pname == 0x8B9D ? 4 : 1)'))},
    'glTexParameterxv': {'params': ('in', P4('(pname == 0x8B9D ? 4 : 1)'))},
    'glGetTexParameterfv': {'params': ('out', P4('(pname == 0x8B9D ? 4 : 1)'))}, 'glGetTexParameteriv': {'params': ('out', P4('(pname == 0x8B9D ? 4 : 1)'))},
    'glGetTexParameterxv': {'params': ('out', P4('(pname == 0x8B9D ? 4 : 1)'))},
    'glPointParameterfv': {'params': ('in', P4('(pname == 0x8129 ? 3 : 1)'))}, 'glPointParameterxv': {'params': ('in', P4('(pname == 0x8129 ? 3 : 1)'))},
    'glGetBufferParameteriv': {'params': ('out', '4')},
    'glDrawTexsvOES': {'coords': ('in', '10')}, 'glDrawTexivOES': {'coords': ('in', '20')}, 'glDrawTexxvOES': {'coords': ('in', '20')},
    'glDrawTexfvOES': {'coords': ('in', '20')},
    'glGetRenderbufferParameterivOES': {'params': ('out', '4')}, 'glGetFramebufferAttachmentParameterivOES': {'params': ('out', '4')},
    'glCompressedTexImage2D': {'data': ('in', 'imageSize')}, 'glCompressedTexSubImage2D': {'data': ('in', 'imageSize')},
}
for name, ret, params in funcs:
    if re.match(r'glDelete\w+s(OES)?$', name): SPEC[name] = {params[1][1]: ('in', params[0][1] + ' * 4')}
    if re.match(r'glGen\w+s(OES)?$', name): SPEC[name] = {params[1][1]: ('out', params[0][1] + ' * 4')}

# written by hand on both sides (guest1.c g1_<name>, host1.c through OP1_* or h1_<name>)
HAND = '''glVertexPointer glColorPointer glNormalPointer glTexCoordPointer glPointSizePointerOES glEnableClientState
glDisableClientState glClientActiveTexture glDrawArrays glDrawElements glGetPointerv glGetString glTexImage2D
glTexSubImage2D glReadPixels glGetBooleanv glGetFloatv glGetIntegerv glGetFixedv glFinish glIsEnabled
glBufferData glBufferSubData'''.split()
# generated, but the guest notes them first (k1_<name> in guest1.c)
HOOK = '''glBindBuffer glDeleteBuffers glPixelStorei'''.split()


def kind(t):
    if '*' in t: return 'ptr'
    if t in ('GLfloat', 'GLclampf'): return 'f'
    return 'i'


def fnptr(f):
    return '(%s (*)(%s))' % (f[1], ', '.join(p[0] for p in f[2]) or 'void')


def proto(f, prefix=''):
    name, ret, params = f
    return '%s %s%s(%s)' % (ret, prefix, name, ', '.join('%s %s' % p for p in params) or 'void')


api = ['/* written by gen1.py */', 'enum {'] + ['    IDX1_%s,' % f[0] for f in funcs] + ['    N1_FUNCS', '};',
       'static const char *const kNames1[N1_FUNCS] = {'] + ['    "%s",' % f[0] for f in funcs] + ['};']
open('gl1_api.h', 'w').write('\n'.join(api) + '\n')

g, h, fwd = ['/* written by gen1.py */'], ['/* written by gen1.py */'], ['/* written by gen1.py */', '#include "fwd1.h"']
for f in funcs:
    name, ret, params = f
    fwd.append('EXPORT %s { %sFWD(%s, %s)(%s); }' % (proto(f), '' if ret == 'void' else 'return ', name, fnptr(f),
                                                     ', '.join(p[1] for p in params)))
    if name in HAND: continue
    spec = dict(SPEC.get(name, {}))
    ptrs = [n for t, n in params if kind(t) == 'ptr']
    if len(ptrs) == 1 and len(spec) == 1 and ptrs[0] not in spec: spec = {ptrs[0]: list(spec.values())[0]}
    for t, n in params:
        if kind(t) == 'ptr' and n not in spec: raise SystemExit('no size for %s %s' % (name, n))
    # ---- guest encoder ----
    size = []
    for t, n in params:
        if kind(t) != 'ptr': size.append('4')
        elif spec[n][0] == 'in': size.append('BLOB1(%s ? (uint32_t)(%s) : NIL)' % (n, spec[n][1]))
    body = []
    if name in HOOK: body.append('k1_%s(%s);' % (name, ', '.join(p[1] for p in params)))
    body.append('uint8_t *q_ = enc1(IDX1_%s, %s);' % (name, ' + '.join(size) or '0'))
    body.append('if (!q_) %s;' % ('return' if ret == 'void' else 'return (%s)0' % ret))
    for t, n in params:
        k = kind(t)
        if k == 'f': body.append('q_ = putf(q_, %s);' % n)
        elif k == 'i': body.append('q_ = put32(q_, (uint32_t)%s);' % n)
        elif spec[n][0] == 'in': body.append('q_ = putblob(q_, %s, %s ? (uint32_t)(%s) : NIL);' % (n, n, spec[n][1]))
    outs = [(t, n) for t, n in params if kind(t) == 'ptr' and spec[n][0] == 'out']
    if outs or ret != 'void':
        body.append('if (!call1()) %s;' % ('return' if ret == 'void' else 'return (%s)0' % ret))
        for t, n in outs: body.append('get1(%s, (uint32_t)(%s));' % (n, spec[n][1]))
        if ret != 'void': body.append('return (%s)get1_32();' % ret)
    g.append('EXPORT %s {\n    %s\n}' % (proto(f), '\n    '.join(body)))
    # ---- host decoder ----
    hb, args = [], []
    for i, (t, n) in enumerate(params):
        k = kind(t)
        if k == 'f': hb.append('%s a%d = rdf(&p);' % (t, i))
        elif k == 'i': hb.append('%s a%d = (%s)rd32(&p);' % (t, i, t))
        elif spec[n][0] == 'in': hb.append('uint32_t n%d; %s a%d = (%s)rdblob(&p, &n%d);' % (i, t, i, t, i))
        else: hb.append('%s a%d = 0;' % (t, i))
        args.append('a%d' % i)
    for i, (t, n) in enumerate(params):        # out blocks: sized from the scalars, which are all read by now
        if kind(t) == 'ptr' and spec[n][0] == 'out':
            e = re.sub(r'\b(%s)\b' % '|'.join(p[1] for p in params), lambda m: 'a%d' % [p[1] for p in params].index(m.group(1)), spec[n][1])
            hb.append('uint32_t o%d = (uint32_t)(%s); a%d = (%s)scratch1(o%d); memset((void *)a%d, 0, o%d);' % (i, e, i, t, i, i, i))
    call = 'F1.%s(%s)' % (name, ', '.join(args))
    hb.append(('%s r = %s;' % (ret, call)) if ret != 'void' else call + ';')
    for i, (t, n) in enumerate(params):
        if kind(t) == 'ptr' and spec[n][0] == 'out': hb.append('reply1((const void *)a%d, o%d);' % (i, i))
    if ret != 'void': hb.append('reply1_32((uint32_t)r);')
    h.append('    case IDX1_%s: { %s break; }' % (name, ' '.join(hb)))

open('g1_enc.h', 'w').write('\n'.join(g) + '\n')
open('g1_fwd.c', 'w').write('\n'.join(fwd) + '\n')
tbl = ['/* written by gen1.py */', 'static struct {'] + ['    %s (*%s)(%s);' % (f[1], f[0], ', '.join(p[0] for p in f[2]) or 'void') for f in funcs] + \
      ['} F1;', 'static void h1_decode(uint32_t op, const uint8_t *p, uint32_t len) {', '    (void)len;', '    switch (op) {'] + h[1:] + \
      ['    default: h1_hand(op, p, len); break;', '    }', '}']
open('h1_dec.h', 'w').write('\n'.join(tbl) + '\n')
print('gen1: %d functions, %d by hand' % (len(funcs), len(HAND)))
