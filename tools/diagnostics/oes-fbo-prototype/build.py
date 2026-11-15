#!/usr/bin/env python3
"""Build an isolated, non-production OES FBO experiment into a new temp directory."""
from pathlib import Path
import argparse
import hashlib
import os
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]


def change(source, old, new):
    if source.count(old) != 1:
        raise ValueError("expected exactly one source fragment: " + old[:100])
    return source.replace(old, new, 1)


def host_experiment(source):
    source = change(source, "struct { GLuint tex; uint64_t id; } tb[64];",
                    "struct { GLuint tex; uint64_t id; int camera_ext; } tb[64];")
    anchor = "static void tb_set(struct hctx *x, GLuint tex, uint64_t id) {"
    source = change(source, anchor, """static uint64_t tb_find_ext(struct hctx *x, GLuint tex) {
    for (int i = 0; tex && i < 64; i++) if (x->tb[i].tex == tex && x->tb[i].camera_ext) return x->tb[i].id;
    return 0;
}
static void tb_set(struct hctx *x, GLuint tex, uint64_t id, int camera_ext) {""")
    source = change(source, "x->tb[i].id = id; if (!id) x->tb[i].tex = 0;",
                    "x->tb[i].id = id; x->tb[i].camera_ext = camera_ext; if (!id) x->tb[i].tex = 0;")
    source = change(source, "x->tb[spare].tex = tex; x->tb[spare].id = id;",
                    "x->tb[spare].tex = tex; x->tb[spare].id = id; x->tb[spare].camera_ext = camera_ext;")
    source = change(source, "tb_set(x, ids[i], 0);", "tb_set(x, ids[i], 0, 0);")
    anchor = "static void fbm_set(struct hctx *x, GLuint fbo, uint64_t id) {"
    source = change(source, anchor, """static void tb_drop_ext(struct hctx *x) {
    GLuint tex = x->gext[x->unit];
    if (tb_find_ext(x, tex)) tb_set(x, tex, 0, 0);
}
static void fbm_set(struct hctx *x, GLuint fbo, uint64_t id) {""")
    anchor = "    glFramebufferTexture2D(target, attachment, textarget, texture, level);\n    if (!x || attachment != GL_COLOR_ATTACHMENT0) return;"
    source = change(source, anchor, """    if (x && attachment == GL_COLOR_ATTACHMENT0 && textarget == 0x8D65 && level == 0 && tb_find_ext(x, texture))
        textarget = GL_TEXTURE_2D;
    glFramebufferTexture2D(target, attachment, textarget, texture, level);
    if (!x || attachment != GL_COLOR_ATTACHMENT0) return;""")
    anchor = "        GLenum target = rd32(&p); uint64_t id = rd64(&p), gen = rd64(&p); int w = (int)rd32(&p), h = (int)rd32(&p);\n        struct hbuf *b = C->ctx && (target == GL_TEXTURE_2D || target == TEX_EXT) ? buf_get(id, w, h, 1) : 0;"
    source = change(source, anchor, """        GLenum target = rd32(&p); uint64_t id = rd64(&p), gen = rd64(&p); int w = (int)rd32(&p), h = (int)rd32(&p);
        int camera_ext = !!(target & 0x80000000u);
        target &= ~0x80000000u;
        if (target != TEX_EXT) camera_ext = 0;
        if (C->ctx && target == TEX_EXT) tb_drop_ext(C->ctx);
        struct hbuf *b = C->ctx && (target == GL_TEXTURE_2D || target == TEX_EXT) ? buf_get(id, w, h, 1) : 0;""")
    source = change(source, "        if (target == GL_TEXTURE_2D) tb_set(C->ctx, C->ctx->g2d[C->ctx->unit], id);",
                    "        if (target == GL_TEXTURE_2D || camera_ext) tb_set(C->ctx, camera_ext ? C->ctx->gext[C->ctx->unit] : C->ctx->g2d[C->ctx->unit], id, camera_ext);")
    source = change(source, "        if (!C->ctx || (target != GL_TEXTURE_2D && target != TEX_EXT) || !d) break;\n        xt(target);",
                    "        if (!C->ctx || (target != GL_TEXTURE_2D && target != TEX_EXT) || !d) break;\n        if (target == TEX_EXT) tb_drop_ext(C->ctx);\n        xt(target);")
    source = change(source, "        GLenum target = rd32(&p); uint32_t id = rd32(&p);\n        EGLImageKHR img = EGL_NO_IMAGE_KHR;",
                    "        GLenum target = rd32(&p); uint32_t id = rd32(&p);\n        if (C->ctx && target == TEX_EXT) tb_drop_ext(C->ctx);\n        EGLImageKHR img = EGL_NO_IMAGE_KHR;")
    return source


def guest_experiment(source):
    anchor = "        begin(T, OP_IMAGE_BIND, 28, 28);\n        put32(T, target); put64(T, note->id); put64(T, count); put32(T, w); put32(T, h);"
    source = change(source, anchor, """        const char *process = prog_name();
        int camera_ext = target == 0x8D65 && b->format == 1 && note->usage == 0x33 && process &&
                         (!strcmp(process, "com.android.camera") || !strncmp(process, "rme_glfbo_", 10));
        begin(T, OP_IMAGE_BIND, 28, 28);
        put32(T, target | (camera_ext ? 0x80000000u : 0)); put64(T, note->id); put64(T, count); put32(T, w); put32(T, h);""")
    source = change(source, "if (bpp && target == 0x0DE1 && !note->pad[1]) note->pad[1] = 1;",
                    "if (bpp && (target == 0x0DE1 || camera_ext) && !note->pad[1]) note->pad[1] = 1;")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", required=True, type=Path)
    args = parser.parse_args()
    platform = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
    compiler = args.ndk / "toolchains" / "llvm" / "prebuilt" / platform / "bin" / ("clang.exe" if os.name == "nt" else "clang")
    if not compiler.is_file():
        raise FileNotFoundError(compiler)
    temp = Path(tempfile.mkdtemp(prefix="rme-oes-fbo-DIAGNOSTIC-"))
    bridge = temp / "native" / "glbridge"
    shutil.copytree(ROOT / "native" / "glbridge", bridge)
    (temp / "native" / "grallocwrap").mkdir()
    shutil.copy2(ROOT / "native" / "grallocwrap" / "note.h", temp / "native" / "grallocwrap" / "note.h")
    (bridge / "host.c").write_text(host_experiment((bridge / "host.c").read_text(encoding="utf-8")), encoding="utf-8")
    (bridge / "guest.c").write_text(guest_experiment((bridge / "guest.c").read_text(encoding="utf-8")), encoding="utf-8")
    for name, source, libs, level in [
        ("libaemugl-DIAGNOSTIC.so", "host.c", ["-lEGL", "-lGLESv3", "-llog", "-lnativewindow", "-landroid"], "29"),
        ("libGLES_aemu-DIAGNOSTIC.so", "guest.c", ["-llog", "-ldl"], "21"),
    ]:
        out = temp / name
        subprocess.run([str(compiler), "--target=aarch64-linux-android" + level, "-O2", "-fPIC", "-shared",
                        "-Wall", "-Wno-unused-function", "-fvisibility=hidden" if source == "guest.c" else "-fvisibility=default",
                        "-Wl,-soname," + ("libGLES_aemu.so" if source == "guest.c" else "libaemugl.so"),
                        str(bridge / source), "-o", str(out), *libs], check=True)
        print(name, out, hashlib.sha256(out.read_bytes()).hexdigest(), flush=True)
    print("DIAGNOSTIC_SOURCE", bridge)


if __name__ == "__main__":
    main()
