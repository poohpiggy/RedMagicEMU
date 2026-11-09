#define _GNU_SOURCE
#include "vibration_metadata_guard.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <linux/magic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/vfs.h>
#include <unistd.h>

#ifndef LANDLOCK_ACCESS_FS_IOCTL_DEV
#define LANDLOCK_ACCESS_FS_IOCTL_DEV (1ULL << 15)
#endif

/* Pin and verify only the two harmless host pseudo-devices needed by the
 * experiment. The rule contains no ioctl right and never allows /dev itself. */
static int experimental_allow_device(int rules_fd, const char *path, unsigned expected_minor, uint64_t rights) {
    int fd = open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st;
    int ok = fstat(fd, &st) == 0 && S_ISCHR(st.st_mode) &&
             major(st.st_rdev) == 1 && minor(st.st_rdev) == expected_minor;
    struct landlock_path_beneath_attr rule = { .allowed_access = rights, .parent_fd = fd };
    if (ok) ok = syscall(SYS_landlock_add_rule, rules_fd, LANDLOCK_RULE_PATH_BENEATH, &rule, 0) == 0;
    close(fd);
    return ok ? 0 : -1;
}

/* /proc/self resolves to this launcher PID, retained after exec. Verify procfs
 * before granting read-only access; do not grant the host /proc directory. */
static int experimental_allow_self_proc(int rules_fd) {
    int fd = open("/proc/self", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct statfs fs;
    int ok = fstatfs(fd, &fs) == 0 && (unsigned long)fs.f_type == PROC_SUPER_MAGIC;
    struct landlock_path_beneath_attr rule = {
        .allowed_access = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR,
        .parent_fd = fd,
    };
    if (ok) ok = syscall(SYS_landlock_add_rule, rules_fd, LANDLOCK_RULE_PATH_BENEATH, &rule, 0) == 0;
    close(fd);
    return ok ? 0 : -1;
}

/* Offline experiment only. An 'I' receipt means these rules were installed;
 * it does not attest Guest authorization or complete hardware isolation.
 * The additional metadata filter and Landlock do not cover every possible
 * syscall or device reachable within an allowed tree. Do not use this mode to
 * enable the HAL. */
static int experimental_vibration_guard(int control, const char *root, const char *engine) {
    if (control < 3 || !root || root[0] != '/' || !engine || engine[0] != '/') return -1;
    for (int i = 0; i < 3; ++i) {
        struct stat st;
        if (fstat(i, &st) || !S_ISFIFO(st.st_mode)) return -1;
    }
    int root_fd = open(root, O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root_fd < 0) return -1;
    int engine_fd = open(engine, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (engine_fd < 0 || fstat(engine_fd, &st) || !S_ISREG(st.st_mode)) {
        if (engine_fd >= 0) close(engine_fd);
        close(root_fd);
        return -1;
    }
    long abi = syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 5) { close(engine_fd); close(root_fd); return -1; }
    const uint64_t rights = (LANDLOCK_ACCESS_FS_IOCTL_DEV << 1) - 1;
    struct landlock_ruleset_attr rules = { .handled_access_fs = rights };
    /* The ABI 1 size works on newer and older supported kernels alike. */
    int rules_fd = (int)syscall(SYS_landlock_create_ruleset, &rules, sizeof(uint64_t), 0);
    if (rules_fd < 0) { close(engine_fd); close(root_fd); return -1; }
    struct landlock_path_beneath_attr rule = { .allowed_access = rights, .parent_fd = root_fd };
    int ok = syscall(SYS_landlock_add_rule, rules_fd, LANDLOCK_RULE_PATH_BENEATH, &rule, 0) == 0;
    rule.allowed_access = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE;
    rule.parent_fd = engine_fd;
    if (ok) ok = syscall(SYS_landlock_add_rule, rules_fd, LANDLOCK_RULE_PATH_BENEATH, &rule, 0) == 0;
    if (ok) ok = experimental_allow_device(rules_fd, "/dev/null", 3,
                                            LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_WRITE_FILE) == 0;
    if (ok) ok = experimental_allow_device(rules_fd, "/dev/urandom", 9, LANDLOCK_ACCESS_FS_READ_FILE) == 0;
    if (ok) ok = experimental_allow_self_proc(rules_fd) == 0;
    if (ok) ok = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0;
    if (ok) ok = syscall(SYS_landlock_restrict_self, rules_fd, 0) == 0;
    close(rules_fd); close(engine_fd); close(root_fd);
    if (!ok) return -1;
#ifdef SYS_close_range
    if (control > 3 && syscall(SYS_close_range, 3U, (unsigned)control - 1, 0U) < 0) return -1;
    if (syscall(SYS_close_range, (unsigned)control + 1, UINT32_MAX, 0U) < 0) return -1;
    if (rme_vibration_metadata_restrict() != 0) return -1;
    return 0;
#else
    return -1;
#endif
}

static int read_all(int fd, void *buf, size_t size) {
    size_t n = 0;
    while (n < size) {
        ssize_t r = read(fd, (char *)buf + n, size - n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        n += (size_t)r;
    }
    return 0;
}

/* A seccomp filter may kill the caller, so probe in this disposable process. */
static int probe_pidfd(void) {
    pid_t child = fork();
    if (child < 0) return 1;
    if (!child) { alarm(3); for (;;) pause(); }
    int fd = (int)syscall(__NR_pidfd_open, child, 0);
    int sent = fd >= 0 && syscall(__NR_pidfd_send_signal, fd, SIGKILL, NULL, 0) == 0;
    if (fd >= 0) close(fd);
    if (!sent) kill(child, SIGKILL); /* still our unreaped child */
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 1;
    return sent && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--probe-pidfd")) return probe_pidfd();
    if (argc < 3) return 125;
    int experiment = !strcmp(argv[2], "--experimental-vibration-guard");
    if (experiment && argc < 5) return 125;
    size_t len = strlen(argv[1]);
    struct sockaddr_un addr = {0};
    if (!len || len >= sizeof(addr.sun_path) - 1) return 125;
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path + 1, argv[1], len);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 125;
    struct timeval timeout = {5, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0 ||
        connect(fd, (struct sockaddr *)&addr, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + len)) < 0)
        return 125;
    char challenge[32], commit;
    if (read_all(fd, challenge, sizeof challenge) < 0 ||
        send(fd, challenge, sizeof challenge, MSG_NOSIGNAL) != sizeof challenge ||
        read_all(fd, &commit, 1) < 0 || commit != 'G') return 125;
    if (experiment) {
        if (experimental_vibration_guard(fd, argv[3], argv[4]) != 0) {
            fputs("experimental vibration rules unavailable\n", stderr);
            return 125;
        }
        if (send(fd, "I", 1, MSG_NOSIGNAL) != 1) return 125;
    }
    close(fd);
    execvp(argv[experiment ? 4 : 2], argv + (experiment ? 4 : 2));
    perror("guest exec");
    return 127;
}
