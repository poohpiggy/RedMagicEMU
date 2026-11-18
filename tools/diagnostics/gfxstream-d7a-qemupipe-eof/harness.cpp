#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <qemu_pipe_bp.h>

static int steps[8], nsteps, index_step, read_offset, write_offset, calls;
static const char payload[] = "abcde";
static char captured[5];

static int next_step() {
    ++calls;
    if (index_step < nsteps) return steps[index_step++];
    return nsteps && steps[nsteps - 1] == 0 ? 0 : -EIO;
}
extern "C" int qemu_pipe_read(int, void* buffer, int size) {
    int n = next_step();
    if (n < 0) errno = -n;
    if (n > size || n > 5 - read_offset) return -EIO;
    if (n > 0) { memcpy(buffer, payload + read_offset, n); read_offset += n; }
    return n;
}
extern "C" int qemu_pipe_write(int, const void* buffer, int size) {
    int n = next_step();
    if (n < 0) errno = -n;
    if (n > size || n > 5 - write_offset) return -EIO;
    if (n > 0) { memcpy(captured + write_offset, buffer, n); write_offset += n; }
    return n;
}
extern "C" int qemu_pipe_try_again(int ret) {
    return ret < 0 && (errno == EINTR || errno == EAGAIN);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const char* which = argv[1];
    char read_buf[5] = {};
    bool reading = strncmp(which, "read-", 5) == 0;
    bool writing = strncmp(which, "write-", 6) == 0;
    if (!reading && !writing) return 2;
    int expected = 0, len = 5;
    if (strstr(which, "-eof")) {
        steps[0] = 0; nsteps = 1; expected = reading ? -EPIPE : -EIO;
    } else if (strstr(which, "-partial")) {
        steps[0] = 2; steps[1] = 0; nsteps = 2; expected = reading ? -EPIPE : -EIO;
    } else if (strstr(which, "-fragment")) {
        steps[0] = 2; steps[1] = 1; steps[2] = 2; nsteps = 3;
    } else if (strstr(which, "-retry")) {
        steps[0] = -EINTR; steps[1] = 5; nsteps = 2;
    } else if (strstr(which, "-again")) {
        steps[0] = -EAGAIN; steps[1] = 5; nsteps = 2;
    } else if (strstr(which, "-error")) {
        steps[0] = -ECONNRESET; nsteps = 1; expected = -ECONNRESET;
    } else if (strstr(which, "-empty")) {
        len = 0;
    } else return 2;
    int got = reading ? qemu_pipe_read_fully(9, read_buf, len)
                      : qemu_pipe_write_fully(9, payload, len);
    if (got != expected || calls != nsteps) {
        fprintf(stderr, "%s: got=%d expected=%d calls=%d expected_calls=%d\n",
                which, got, expected, calls, nsteps);
        return 1;
    }
    if (expected == 0 && len && memcmp(reading ? read_buf : captured, payload, len)) return 1;
    if (strstr(which, "-partial") && memcmp(reading ? read_buf : captured, payload, 2)) return 1;
    return 0;
}
