#!/usr/bin/env python3
"""Exercise the actual host FBO attachment function from a selected source."""
from pathlib import Path
import argparse
import subprocess
import tempfile


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if not depth:
                return source[start:i + 1]
    raise ValueError("unterminated function " + signature)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host_source", type=Path)
    args = parser.parse_args()
    source = args.host_source.read_text(encoding="utf-8")
    ext = function(source, "static uint64_t tb_find_ext(") if "static uint64_t tb_find_ext(" in source else ""
    parts = [r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_TEXTURE_2D 0x0DE1
#define TEX_EXT 0x8D65
struct hctx { GLuint draw_fb, read_fb; struct { GLuint tex; uint64_t id; int camera_ext; } tb[64]; };
struct conn { struct hctx *ctx; } conn;
static struct conn *C = &conn;
static GLenum called_target;
static void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    (void)target; (void)attachment; (void)texture; (void)level; called_target = textarget;
}
static void fbm_set(struct hctx *x, GLuint fb, uint64_t id) { (void)x; (void)fb; (void)id; }
static void buf_mark(uint64_t id) { (void)id; }
''', function(source, "static uint64_t tb_find("), ext,
        function(source, "static void h_glFramebufferTexture2D("), r'''
static int failures;
static void check(GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLenum expected, const char *name) {
    called_target = 0;
    h_glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, textarget, texture, level);
    if (called_target != expected) { fprintf(stderr, "FAIL: %s got 0x%x\n", name, called_target); failures++; }
}
int main(void) {
    struct hctx ctx = {0}; conn.ctx = &ctx; ctx.draw_fb = 1;
    ctx.tb[0].tex = 10; ctx.tb[0].id = 42; ctx.tb[0].camera_ext = 1;
    ctx.tb[1].tex = 11; ctx.tb[1].id = 43; ctx.tb[1].camera_ext = 0;
    check(GL_COLOR_ATTACHMENT0, TEX_EXT, 10, 0, GL_TEXTURE_2D, "diagnostic OES AHB attaches using host 2D");
    check(GL_COLOR_ATTACHMENT0, TEX_EXT, 11, 0, TEX_EXT, "ordinary 2D AHB is not converted");
    check(GL_COLOR_ATTACHMENT0, TEX_EXT, 12, 0, TEX_EXT, "unknown texture is not converted");
    check(GL_COLOR_ATTACHMENT0 + 1, TEX_EXT, 10, 0, TEX_EXT, "other attachments are not converted");
    check(GL_COLOR_ATTACHMENT0, TEX_EXT, 10, 1, TEX_EXT, "nonzero mip level is not converted");
    check(GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 10, 0, GL_TEXTURE_2D, "2D attachment passes through");
    if (!failures) puts("diagnostic OES framebuffer gating: OK");
    return failures != 0;
}
''']
    with tempfile.TemporaryDirectory(prefix="rme-oes-test-") as tmp:
        src = Path(tmp) / "test.c"
        exe = Path(tmp) / "test"
        src.write_text("\n".join(parts), encoding="utf-8")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
