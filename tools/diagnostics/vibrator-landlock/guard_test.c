#define _GNU_SOURCE
#include "guard.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <linux/openat2.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static void safe_path(char *out, size_t n, const char *parent, const char *name) {
    int written = snprintf(out, n, "%s/%s", parent, name);
    assert(written > 0 && (size_t)written < n);
}

int main(void) {
    long abi = syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 5) { puts("VIBRATOR_LANDLOCK_SKIPPED_UNAVAILABLE"); return 77; }
#ifdef SYS_close_range
    if (syscall(SYS_close_range, UINT_MAX, UINT_MAX, 0) < 0 &&
        (errno == ENOSYS || errno == EPERM)) {
        puts("VIBRATOR_LANDLOCK_SKIPPED_CLOSE_RANGE"); return 77;
    }
#else
    puts("VIBRATOR_LANDLOCK_SKIPPED_CLOSE_RANGE"); return 77;
#endif
    int socket_probe = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (socket_probe < 0 && (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT ||
                             errno == EOPNOTSUPP || errno == EPERM)) {
        puts("VIBRATOR_LANDLOCK_UNIX_SOCKETS_SKIPPED"); return 77;
    }
    assert(socket_probe >= 0);
    struct sockaddr_un abstract_probe = { .sun_family = AF_UNIX };
    int probe_len = snprintf(abstract_probe.sun_path + 1, sizeof abstract_probe.sun_path - 1,
                             "rme-vib-probe-%ld", (long)getpid());
    assert(probe_len > 0 && (size_t)probe_len < sizeof abstract_probe.sun_path - 1);
    socklen_t probe_addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + probe_len);
    if (bind(socket_probe, (struct sockaddr *)&abstract_probe, probe_addr_len) < 0) {
        int error = errno; close(socket_probe);
        if (error == EAFNOSUPPORT || error == EPROTONOSUPPORT || error == EOPNOTSUPP ||
            error == EPERM || error == EINVAL) {
            puts("VIBRATOR_LANDLOCK_UNIX_SOCKETS_SKIPPED"); return 77;
        }
        errno = error; perror("abstract socket probe"); return 1;
    }
    close(socket_probe);
    char tmp[] = "/tmp/rme-vib-landlock-XXXXXX";
    assert(mkdtemp(tmp));
    char filesystem_probe_path[PATH_MAX]; safe_path(filesystem_probe_path, sizeof filesystem_probe_path, tmp, "probe.sock");
    struct sockaddr_un filesystem_probe = { .sun_family = AF_UNIX };
    assert(strlen(filesystem_probe_path) < sizeof filesystem_probe.sun_path);
    strcpy(filesystem_probe.sun_path, filesystem_probe_path);
    int filesystem_probe_fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(filesystem_probe_fd >= 0);
    socklen_t filesystem_probe_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(filesystem_probe_path) + 1);
    if (bind(filesystem_probe_fd, (struct sockaddr *)&filesystem_probe, filesystem_probe_len) < 0) {
        int error = errno; close(filesystem_probe_fd); assert(rmdir(tmp) == 0);
        if (error == EAFNOSUPPORT || error == EPROTONOSUPPORT || error == EOPNOTSUPP ||
            error == EPERM || error == EACCES || error == EROFS || error == EINVAL) {
            puts("VIBRATOR_LANDLOCK_UNIX_SOCKETS_SKIPPED"); return 77;
        }
        errno = error; perror("filesystem socket probe"); return 1;
    }
    close(filesystem_probe_fd); assert(unlink(filesystem_probe_path) == 0);
    char guest[PATH_MAX], outside[PATH_MAX], good[PATH_MAX], bad[PATH_MAX], link[PATH_MAX];
    safe_path(guest, sizeof guest, tmp, "guest");
    safe_path(outside, sizeof outside, tmp, "outside");
    assert(mkdir(guest, 0700) == 0 && mkdir(outside, 0700) == 0);
    safe_path(good, sizeof good, guest, "file");
    safe_path(bad, sizeof bad, outside, "file");
    safe_path(link, sizeof link, guest, "escape");
    int fd = open(good, O_CREAT | O_WRONLY | O_CLOEXEC, 0600); assert(fd >= 0); close(fd);
    fd = open(bad, O_CREAT | O_WRONLY | O_CLOEXEC, 0600); assert(fd >= 0); close(fd);
    assert(symlink(bad, link) == 0);
    int dirfd = open(guest, O_PATH | O_DIRECTORY | O_CLOEXEC); assert(dirfd >= 0);
    int outside_dirfd = open(outside, O_PATH | O_DIRECTORY | O_CLOEXEC); assert(outside_dirfd >= 0);
    int inherited = open(bad, O_RDWR | O_CLOEXEC); assert(inherited >= 0);
    int retained_root = fcntl(dirfd, F_DUPFD_CLOEXEC, 10); assert(retained_root >= 10);
    int high_external = fcntl(inherited, F_DUPFD_CLOEXEC, retained_root + 1); assert(high_external > retained_root);
#ifdef SYS_openat2
    struct open_how probe = { .flags = O_RDONLY | O_CLOEXEC };
    long available = syscall(SYS_openat2, AT_FDCWD, good, &probe, sizeof probe);
    if (available >= 0) close((int)available);
    else assert(errno == ENOSYS || errno == EPERM || errno == EOPNOTSUPP);
#endif
    pid_t p = fork(); assert(p >= 0);
    if (!p) {
        int result = rme_vib_landlock(dirfd, 5);
        assert(result == 0);
        int inside = open(good, O_RDWR | O_CLOEXEC); assert(inside >= 0);
        assert(write(inside, "x", 1) == 1); close(inside);
        errno = 0; assert(open(bad, O_RDONLY | O_CLOEXEC) == -1 && errno == EACCES);
        errno = 0; assert(openat(outside_dirfd, "file", O_RDONLY | O_CLOEXEC) == -1 && errno == EACCES);
        errno = 0; assert(open(link, O_RDONLY | O_CLOEXEC) == -1 && errno == EACCES);
        /* A pre-opened FD is not repaired by sandboxing: the owner must prevent it. */
        assert(pwrite(inherited, "x", 1, 0) == 1);
#ifdef SYS_openat2
        if (available >= 0) {
            struct open_how how = { .flags = O_RDONLY | O_CLOEXEC };
            errno = 0;
            long r = syscall(SYS_openat2, AT_FDCWD, bad, &how, sizeof how);
            assert(r == -1 && errno == EACCES);
        }
#endif
        _exit(0);
    }
    int status; assert(waitpid(p, &status, 0) == p);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    /* A fresh single-threaded child must close inherited external FDs while
     * retaining only the known Guest root and preexisting standard streams. */
    p = fork(); assert(p >= 0);
    if (!p) {
        int std_open[3];
        for (int i = 0; i < 3; ++i) std_open[i] = fcntl(i, F_GETFD) >= 0;
        assert(rme_vib_close_unneeded_fds(-1) == -EBADF);
        assert(rme_vib_close_unneeded_fds(0) == -EINVAL);
        assert(rme_vib_close_unneeded_fds(inherited) == -ENOTDIR);
        assert(rme_vib_close_unneeded_fds(retained_root) == 0);
        assert(fcntl(retained_root, F_GETFD) >= 0);
        for (int i = 0; i < 3; ++i) assert((fcntl(i, F_GETFD) >= 0) == std_open[i]);
        errno = 0; assert(fcntl(dirfd, F_GETFD) == -1 && errno == EBADF);
        errno = 0; assert(fcntl(inherited, F_GETFD) == -1 && errno == EBADF);
        errno = 0; assert(fcntl(outside_dirfd, F_GETFD) == -1 && errno == EBADF);
        errno = 0; assert(fcntl(high_external, F_GETFD) == -1 && errno == EBADF);
        assert(rme_vib_landlock(retained_root, 5) == 0);
        int inside = open(good, O_RDWR | O_CLOEXEC); assert(inside >= 0); close(inside);
        errno = 0; assert(open(bad, O_RDONLY | O_CLOEXEC) == -1 && errno == EACCES);
        _exit(0);
    }
    assert(waitpid(p, &status, 0) == p && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    p = fork(); assert(p >= 0);
    if (!p) {
        assert(rme_vib_landlock(dirfd, UINT_MAX) < 0);
        /* Rejection alone does not confine; caller must not start the HAL. */
        int still_open = open(bad, O_RDONLY | O_CLOEXEC); assert(still_open >= 0); close(still_open);
        _exit(0);
    }
    assert(waitpid(p, &status, 0) == p && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    /* The real Binder emulator connects via a filesystem socket outside the
     * Guest root; the vibration bridge uses an abstract UNIX socket. Check
     * each connection after removing inherited descriptors and confining a
     * self-created child. The payload and peers are test-only. */
    char sock_path[PATH_MAX]; safe_path(sock_path, sizeof sock_path, outside, "binder.sock");
    struct sockaddr_un filesystem = { .sun_family = AF_UNIX };
    assert(strlen(sock_path) < sizeof filesystem.sun_path);
    strcpy(filesystem.sun_path, sock_path);
    socklen_t filesystem_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(sock_path) + 1);
    int filesystem_listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(filesystem_listener >= 0);
    assert(bind(filesystem_listener, (struct sockaddr *)&filesystem, filesystem_len) == 0);
    assert(listen(filesystem_listener, 1) == 0);
    struct sockaddr_un abstract = { .sun_family = AF_UNIX };
    int name_len = snprintf(abstract.sun_path + 1, sizeof abstract.sun_path - 1,
                            "rme-vib-landlock-%ld", (long)getpid());
    assert(name_len > 0 && (size_t)name_len < sizeof abstract.sun_path - 1);
    socklen_t abstract_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + name_len);
    int abstract_listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    assert(abstract_listener >= 0);
    assert(bind(abstract_listener, (struct sockaddr *)&abstract, abstract_len) == 0);
    assert(listen(abstract_listener, 1) == 0);
    p = fork(); assert(p >= 0);
    if (!p) {
        alarm(5);
        assert(rme_vib_close_unneeded_fds(retained_root) == 0);
        errno = 0; assert(fcntl(filesystem_listener, F_GETFD) == -1 && errno == EBADF);
        errno = 0; assert(fcntl(abstract_listener, F_GETFD) == -1 && errno == EBADF);
        assert(rme_vib_landlock(retained_root, 5) == 0);
        const struct sockaddr_un *addresses[] = { &filesystem, &abstract };
        const socklen_t lengths[] = { filesystem_len, abstract_len };
        for (int i = 0; i < 2; ++i) {
            int s = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0); assert(s >= 0);
            assert(connect(s, (const struct sockaddr *)addresses[i], lengths[i]) == 0);
            assert(write(s, "x", 1) == 1);
            char reply; assert(read(s, &reply, 1) == 1 && reply == 'y'); close(s);
        }
        _exit(0);
    }
    alarm(5);
    int listeners[] = { filesystem_listener, abstract_listener };
    for (int i = 0; i < 2; ++i) {
        int peer = accept4(listeners[i], NULL, NULL, SOCK_CLOEXEC); assert(peer >= 0);
        char message; assert(read(peer, &message, 1) == 1 && message == 'x');
        assert(write(peer, "y", 1) == 1); close(peer);
    }
    assert(waitpid(p, &status, 0) == p && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    alarm(0);
    close(filesystem_listener); close(abstract_listener); assert(unlink(sock_path) == 0);
    puts("VIBRATOR_LANDLOCK_UNIX_SOCKETS_OK");
    close(dirfd); close(outside_dirfd); close(inherited); close(retained_root); close(high_external);
    assert(unlink(link) == 0 && unlink(good) == 0 && unlink(bad) == 0);
    assert(rmdir(guest) == 0 && rmdir(outside) == 0 && rmdir(tmp) == 0);
#ifdef SYS_openat2
    puts(available >= 0 ? "VIBRATOR_LANDLOCK_OPENAT2_OK" : "VIBRATOR_LANDLOCK_OPENAT2_SKIPPED");
#endif
    puts("VIBRATOR_LANDLOCK_GUARD_OK");
}
