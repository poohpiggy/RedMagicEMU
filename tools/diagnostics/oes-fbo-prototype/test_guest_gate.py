#!/usr/bin/env python3
"""Exercise the diagnostic guest's actual OES AHB gating expression."""
from pathlib import Path
import argparse
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("guest_source", type=Path)
    source = parser.parse_args().guest_source.read_text(encoding="utf-8")
    start = source.index("        const char *process = prog_name();", source.index("static void e_glEGLImageTargetTexture2DOES("))
    end = source.index("        begin(T, OP_IMAGE_BIND", start)
    gate = source[start:end]
    harness = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct image { int format; };
struct meta_note { uint64_t usage; };
static const char *name;
static const char *prog_name(void) { return name; }
static int camera_gate(uint32_t target, struct image *b, struct meta_note *note) {
'''+gate+r'''
    return camera_ext;
}
static int failures;
static void check(int actual, int expected, const char *why) {
    if (actual!=expected) { fprintf(stderr,"FAIL: %s\n",why); failures++; }
}
int main(void) {
    struct image image={1}; struct meta_note note={0x33};
    name="com.android.camera";
    check(camera_gate(0x8D65,&image,&note),1,"stock camera RGBA/OES AHB selected");
    name="rme_glfbo_1234";
    check(camera_gate(0x8D65,&image,&note),1,"isolated pixel probe selected");
    name="other.camera";
    check(camera_gate(0x8D65,&image,&note),0,"other process unchanged");
    name="com.android.camera:remote";
    check(camera_gate(0x8D65,&image,&note),0,"suffixed process unchanged");
    name=0;
    check(camera_gate(0x8D65,&image,&note),0,"unknown process unchanged");
    name="com.android.camera";
    check(camera_gate(0x0DE1,&image,&note),0,"2D image not selected as OES");
    image.format=4;
    check(camera_gate(0x8D65,&image,&note),0,"RGB565 image unchanged");
    image.format=1; note.usage=0x30;
    check(camera_gate(0x8D65,&image,&note),0,"different AHB usage unchanged");
    if (!failures) puts("diagnostic guest process and AHB gating: OK");
    return failures != 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="rme-oes-guest-test-") as tmp:
        src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
        src.write_text(harness, encoding="utf-8")
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
