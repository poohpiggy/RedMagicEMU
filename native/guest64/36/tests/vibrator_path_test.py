"""Exercise production path translation for missing vibration sysfs entries.

The translator targets ARM64, so compile its actual C path functions in a small
host harness. The fake access call reports that the Guest entry is absent.
This checks absolute lexical paths only; symlinks and relative paths need
separate isolation guarantees before a real HAL is enabled.
"""

from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "run64.c").read_text(encoding="utf-8")
path_functions = source[source.index("static int norm("):source.index("// host path → guest path")]
assert "static const char *xl(" in path_functions

harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned long u64;
#define PATH_MAX 4096
#define ENAMETOOLONG 36
#define AT_FDCWD (-100)
#define NR_faccessat 48
#define S4(n, a, b, c, d) (path_present ? 0L : -1L)
#define S0(n) (123L)
static int path_present;
static u64 slen(const char *s) { return strlen(s); }
static int seq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static int pre(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
static char g_root[512] = "/guest";
static u64 g_rootlen = 6;
static char g_props[512];
static char g_sd[512];
static u64 g_sdlen;
static int g_is32;
''' + path_functions + r'''
static void expect(const char *input, const char *output) {
    char buf[PATH_MAX * 3];
    int err = 0;
    const char *actual = xl(input, buf, &err);
    assert(err == 0 && actual && strcmp(actual, output) == 0);
}
int main(void) {
    expect("/sys/class/leds/zte_vibrator/duration_aw", "/guest/sys/class/leds/zte_vibrator/duration_aw");
    expect("/sys/class/leds/vibrator/activate_aw", "/guest/sys/class/leds/vibrator/activate_aw");
    expect("/sys/class/qcom-haptics/unknown", "/guest/sys/class/qcom-haptics/unknown");
    expect("/sys/class/leds/zte_vibrator", "/guest/sys/class/leds/zte_vibrator");
    expect("/sys//class/leds/zte_vibrator/./activate_aw", "/guest/sys/class/leds/zte_vibrator/activate_aw");
    expect("/sys/class/leds/zte_vibrator_extra/activate_aw", "/sys/class/leds/zte_vibrator_extra/activate_aw");
    expect("/sys/class/qcom-haptics-extra/unknown", "/sys/class/qcom-haptics-extra/unknown");
    expect("/sys/class/leds/other/activate_aw", "/sys/class/leds/other/activate_aw");
    path_present = 1;
    expect("/sys/class/leds/other/activate_aw", "/guest/sys/class/leds/other/activate_aw");
    path_present = 0;
    g_is32 = 1;
    expect("/sys/class/leds/zte_vibrator/activate_aw", "/guest/sys/class/leds/zte_vibrator/activate_aw");
    puts("VIBRATOR_ABSOLUTE_PATH_FALLBACK_OK");
}
'''

with tempfile.TemporaryDirectory(prefix="rme-vibrator-path-") as tmp:
    temp = Path(tmp)
    c_file = temp / "harness.c"
    binary = temp / "harness"
    c_file.write_text(harness, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c_file), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
