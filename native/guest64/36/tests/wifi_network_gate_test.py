"""Compile the API 36 Wi-Fi gate decisions and verify send-path wiring.

Run under Linux (including WSL): python3 native/guest64/36/tests/wifi_network_gate_test.py
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parent.parent / "run64.c").read_text(encoding="utf-8")


def extract(start: str, end: str) -> str:
    left = source.index(start)
    return source[left:source.index(end, left)]


helpers = extract("static int network_gate_allows_byte", "static long trap2")
decisions = extract("static int network_gate_allows_byte", "/* Missing means Ethernet mode.")
gate_check = extract("static int network_gate_blocked", "static int network_gate_internet_fd")
assert "AEMU_NETWORK_GATE_PATH" in helpers
assert "network_gate_blocked" in helpers
assert "g_network_gate_fd" not in source, "the gate must not retain an FD that zygote's allowlist rejects"
assert "S4(NR_openat" in gate_check
assert "S1(NR_close" in gate_check

for case in ("case NR_bind: case NR_connect:", "case NR_sendto:", "case NR_sendmsg:", "case NR_writev:"):
    chunk = extract(case, "\n    case " ) if case != "case NR_writev:" else extract(case, "\n    case ")
    assert "network_gate_blocked()" in chunk, f"{case} bypasses the virtual Wi-Fi gate"

program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned long u64;
typedef unsigned short u16;
typedef unsigned char u8;
''' + decisions + r'''
int main(void) {
    assert(network_gate_allows_byte(1, '1'));
    assert(!network_gate_allows_byte(0, 0));
    assert(!network_gate_allows_byte(1, '0'));
    assert(!network_gate_allows_byte(2, '1'));

    u8 ipv4[] = { 2, 0, 0, 80, 8, 8, 8, 8 };
    assert(network_gate_external_address(ipv4, sizeof ipv4));
    ipv4[4] = 127; ipv4[7] = 1;
    assert(!network_gate_external_address(ipv4, sizeof ipv4));

    u8 ipv6[24] = { 10, 0 };
    ipv6[23] = 1;
    assert(!network_gate_external_address(ipv6, sizeof ipv6));
    ipv6[0] = 2;
    ipv6[23] = 1;
    assert(network_gate_external_address(ipv6, sizeof ipv6));

    u8 unix_socket[] = { 1, 0, 42 };
    assert(!network_gate_external_address(unix_socket, sizeof unix_socket));
    assert(!network_gate_external_address(NULL, 0));
    puts("WIFI_NETWORK_GATE_DECISIONS_OK");
}
'''

with tempfile.TemporaryDirectory(prefix="rme-wifi-gate-test-") as temporary:
    root = Path(temporary)
    cfile, binary = root / "test.c", root / "test"
    cfile.write_text(program, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(cfile), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
