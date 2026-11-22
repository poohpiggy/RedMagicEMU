"""Check that a fatal report can exit with a stale frame pointer."""
from pathlib import Path
import subprocess
import tempfile


source = (Path(__file__).resolve().parent.parent / 'run64.c').read_text()
start = source.index('    say("  backtrace:\\n");')
walk = source[start:source.index('\n}', start)]
harness = r'''
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

typedef uint64_t u64;
static void say(const char *text) { fputs(text, stdout); }
static void where(u64 lr) { printf("call %llx\n", (unsigned long long)lr); }
#define S0(nr) getpid()
#define S1(nr, status) _exit(status)
#define S6(nr, pid, local, liovcnt, remote, riovcnt, flags) \
    process_vm_readv((pid), (void *)(local), (liovcnt), (void *)(remote), (riovcnt), (flags))

static void report(u64 first_frame, int sig) {
    struct { u64 regs[31]; } regs = {0}, *c = &regs;
    c->regs[29] = first_frame;
''' + walk + r'''
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    u64 *frame = mmap(NULL, page * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (frame == MAP_FAILED || mprotect((char *)frame + page, page, PROT_NONE))
        return 2;
    frame[0] = (u64)((char *)frame + page);
    frame[1] = 0x1234;
    report((u64)frame, SIGABRT);
    return 3;
}
'''

with tempfile.TemporaryDirectory(prefix='rme-crash-backtrace-') as temp:
    path = Path(temp)
    c_file, binary = path / 'test.c', path / 'test'
    c_file.write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c_file), '-o', str(binary)], check=True)
    result = subprocess.run([binary], text=True, capture_output=True, timeout=4)
    assert (result.returncode, result.stdout) == (134, '  backtrace:\ncall 1234\n'), result
print('FATAL_REPORT_EXITS_WITH_STALE_FRAME_OK')
