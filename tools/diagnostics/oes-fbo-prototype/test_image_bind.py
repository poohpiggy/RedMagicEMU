#!/usr/bin/env python3
"""Exercise the selected host's real OP_IMAGE_BIND and texture bookkeeping."""
from pathlib import Path
import argparse
import subprocess
import tempfile
from test_attachment import function


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host_source", type=Path)
    source = parser.parse_args().host_source.read_text(encoding="utf-8")
    case = source[source.index("    case OP_IMAGE_BIND: {"):source.index("    case OP_IMAGE_CREATE: {")]
    stub = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned GLenum;
typedef unsigned GLuint;
#define GL_TEXTURE_2D 0x0DE1u
#define TEX_EXT 0x8D65u
#define AEMU_IMAGE_BIND_CAMERA_FBO 0x80000000u
#define OP_IMAGE_BIND 1
struct hctx { GLuint g2d[32], gext[32]; int unit; struct { GLuint tex; uint64_t id; int camera_ext; } tb[64]; };
struct hbuf { uint64_t id, gen, frame; void *img; } buffer;
struct conn { struct hctx *ctx; } connection;
static struct conn *C = &connection;
static int reply_value, image_calls, fail_get;
static uint32_t rd32(const uint8_t **p) { uint32_t v; memcpy(&v,*p,4); *p+=4; return v; }
static uint64_t rd64(const uint8_t **p) { uint64_t v; memcpy(&v,*p,8); *p+=8; return v; }
static struct hbuf *buf_get(uint64_t id, int w, int h, int make) {
    (void)w; (void)h; (void)make; if (fail_get) return 0; buffer.id=id; return &buffer;
}
static GLenum xt(GLenum t) { return t; }
static void glEGLImageTargetTexture2DOES(GLenum t, void *img) { (void)t; (void)img; image_calls++; }
static void reply32(uint32_t v) { reply_value=(int)v; }
'''
    setup = r'''
static void dispatch(const uint8_t *p) { switch (OP_IMAGE_BIND) {
'''
    cases = r'''
} }
static void put32(uint8_t **p, uint32_t v) { memcpy(*p,&v,4); *p+=4; }
static void put64(uint8_t **p, uint64_t v) { memcpy(*p,&v,8); *p+=8; }
static void send_bind(uint32_t target, uint64_t id) {
    uint8_t data[28], *p=data; put32(&p,target); put64(&p,id); put64(&p,0); put32(&p,10); put32(&p,6);
    dispatch(data);
}
static int failures;
static void check(int ok, const char *what) { if (!ok) { fprintf(stderr,"FAIL: %s\n",what); failures++; } }
int main(void) {
    struct hctx a={0}, b={0}; connection.ctx=&a; a.gext[0]=10; a.g2d[0]=11;
    send_bind(TEX_EXT | 0x80000000u,42);
    check(a.tb[0].tex==10 && a.tb[0].id==42 && a.tb[0].camera_ext==1 && image_calls==1,
          "marked OES binds its actual texture to AHB");
    send_bind(GL_TEXTURE_2D,43);
    check(a.tb[1].tex==11 && a.tb[1].id==43 && !a.tb[1].camera_ext, "ordinary 2D mapping unchanged");
    connection.ctx=&b; b.gext[0]=10;
    send_bind(TEX_EXT,44);
    check(b.tb[0].tex==0 && a.tb[0].id==42, "unmarked OES and other contexts do not acquire mapping");
    connection.ctx=&a; send_bind(TEX_EXT,44);
    check(!a.tb[0].tex, "replacing a marked texture with unmarked image clears old buffer mapping");
    send_bind(TEX_EXT | 0x80000000u,42);
    fail_get=1; send_bind(TEX_EXT | 0x80000000u,45);
    check(!a.tb[0].tex, "failed rebind clears old buffer mapping");
    a.gext[0]=12; send_bind(TEX_EXT | 0x80000000u,45);
    check(reply_value==0 && !a.tb[2].tex, "failed image buffer lookup is not registered");
    if (!failures) puts("diagnostic OES image binding: OK");
    return failures != 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="rme-oes-bind-test-") as tmp:
        src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
        lookup = function(source, "static uint64_t tb_find_ext(") if "static uint64_t tb_find_ext(" in source else ""
        drop = function(source, "static void tb_drop_ext(") if "static void tb_drop_ext(" in source else ""
        src.write_text(stub + lookup + function(source, "static void tb_set(") + drop + setup + case + cases, encoding="utf-8")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
