#!/usr/bin/env python3
"""Exercise the real GLES encoder's pixel-store state and encoded commands.

Run with Python 3 and a host C compiler, for example in WSL:
    python3 native/glbridge/tests/test_pixel_store.py
No Android runtime, device, or generated asset is modified.
"""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


BRIDGE = Path(__file__).resolve().parent.parent


def function_source(source: str, signature: str) -> str:
    start = source.index(signature)
    first_brace = source.index("{", start)
    depth = 0
    for pos in range(first_brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start:pos + 1]
    raise ValueError(f"unterminated function: {signature}")


PRELUDE = r"""
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned int GLenum;
typedef int GLint;
struct gstate {
    int p_align, p_row, p_skip_rows, p_skip_pix;
    int u_align, u_row, u_skip_rows, u_skip_pix, u_img_h, u_skip_img;
};
struct ts { int unused; };
static struct gstate state;
static struct ts thread;
static unsigned op, fixed_len, total_len, words[2], words_count, calls;
static struct gstate *G(void) { return &state; }
static struct ts *TS(void) { return &thread; }
static void begin(struct ts *t, uint32_t opcode, uint32_t fixed, uint32_t total) {
    (void)t; op = opcode; fixed_len = fixed; total_len = total; ++calls;
}
static void put32(struct ts *t, uint32_t value) {
    (void)t;
    if (words_count < 2) words[words_count] = value;
    ++words_count;
}
"""

CASES = r"""
static int failures;
static void send(GLenum pname, GLint value) {
    op = fixed_len = total_len = words_count = calls = 0;
    memset(words, 0, sizeof(words));
    e_glPixelStorei(pname, value);
    if (calls != 1 || op != 90 || fixed_len != 8 || total_len != 8 ||
        words_count != 2 || words[0] != pname || words[1] != (uint32_t)value) {
        fprintf(stderr, "FAIL command not forwarded unchanged: pname=0x%x value=%d\n", pname, value);
        ++failures;
    }
}
static void expect_field(const char *name, int got, int expected) {
    if (got != expected) {
        fprintf(stderr, "FAIL %s: got %d expected %d\n", name, got, expected);
        ++failures;
    }
}
int main(void) {
    const int legal[] = {1, 2, 4, 8};
    const int bad[] = {0, 3, -1};
    const struct { GLenum pname; int *field; const char *name; } alignment[] = {
        {0x0d05, &state.p_align, "pack alignment"},
        {0x0cf5, &state.u_align, "unpack alignment"},
    };
    const struct { GLenum pname; int *field; const char *name; } dimension[] = {
        {0x0d02, &state.p_row, "pack row length"},
        {0x0d03, &state.p_skip_rows, "pack skip rows"},
        {0x0d04, &state.p_skip_pix, "pack skip pixels"},
        {0x0cf2, &state.u_row, "unpack row length"},
        {0x0cf3, &state.u_skip_rows, "unpack skip rows"},
        {0x0cf4, &state.u_skip_pix, "unpack skip pixels"},
        {0x806e, &state.u_img_h, "unpack image height"},
        {0x806d, &state.u_skip_img, "unpack skip images"},
    };
    for (unsigned i = 0; i < sizeof(alignment) / sizeof(alignment[0]); ++i) {
        for (unsigned j = 0; j < sizeof(legal) / sizeof(legal[0]); ++j) {
            send(alignment[i].pname, legal[j]);
            expect_field(alignment[i].name, *alignment[i].field, legal[j]);
        }
        for (unsigned j = 0; j < sizeof(bad) / sizeof(bad[0]); ++j) {
            send(alignment[i].pname, 4);
            send(alignment[i].pname, bad[j]);
            expect_field(alignment[i].name, *alignment[i].field, 4);
        }
    }
    for (unsigned i = 0; i < sizeof(dimension) / sizeof(dimension[0]); ++i) {
        send(dimension[i].pname, 7);
        expect_field(dimension[i].name, *dimension[i].field, 7);
        send(dimension[i].pname, -1);
        expect_field(dimension[i].name, *dimension[i].field, 7);
        send(dimension[i].pname, 0);
        expect_field(dimension[i].name, *dimension[i].field, 0);
    }
    send(0xdead, -1); /* unknown names are forwarded too */
    send(0x0cf5, 4);
    send(0x0cf5, 3);
    uint32_t n = pix_bytes(3, 2, 1, 0x1907, 0x1401,
                           state.u_align, state.u_row, state.u_skip_rows,
                           state.u_skip_pix, state.u_img_h, state.u_skip_img);
    if (n != 21) { fprintf(stderr, "FAIL 3x2 RGB8 payload: got %u expected 21\n", n); ++failures; }
    send(0x0d05, 4);
    send(0x0d05, 3);
    n = pix_bytes(3, 2, 1, 0x1907, 0x1401,
                  state.p_align, state.p_row, state.p_skip_rows,
                  state.p_skip_pix, 0, 0);
    if (n != 21) { fprintf(stderr, "FAIL 3x2 RGB8 readback: got %u expected 21\n", n); ++failures; }
    if (failures) { fprintf(stderr, "PIXEL_STORE_FAILURES=%d\n", failures); return 1; }
    puts("PIXEL_STORE_ENCODER_OK");
    return 0;
}
"""


class PixelStoreEncoderTest(unittest.TestCase):
    def test_state_validation_and_command_forwarding(self):
        guest = (BRIDGE / "guest.c").read_text(encoding="utf-8")
        generated = (BRIDGE / "g_api.h").read_text(encoding="utf-8")
        source = "\n".join((
            PRELUDE,
            function_source(guest, "static void k_glPixelStorei("),
            function_source(guest, "static uint32_t pix_bytes("),
            function_source(generated, "static void e_glPixelStorei("),
            CASES,
        ))
        with tempfile.TemporaryDirectory(prefix="rme-pixel-store-") as temp:
            src = Path(temp) / "test.c"
            exe = Path(temp) / "test"
            src.write_text(source, encoding="utf-8")
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", str(src), "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PIXEL_STORE_ENCODER_OK", result.stdout)


if __name__ == "__main__":
    unittest.main()
