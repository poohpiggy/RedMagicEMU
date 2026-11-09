#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Demonstration only: this is not a complete production sandbox filter. */
#if defined(__x86_64__)
#define TEST_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define TEST_ARCH AUDIT_ARCH_AARCH64
#else
#error Unsupported test architecture
#endif
#define DENY(n) BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (n), 0, 1), \
                BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)

int main(int argc, char **argv) {
    if (argc != 4) return 1;
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, TEST_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
#if defined(__x86_64__)
        BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000U, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
#endif
        DENY(SYS_mount), DENY(SYS_umount2), DENY(SYS_unshare), DENY(SYS_setns),
        DENY(SYS_mknodat), DENY(428), DENY(429), DENY(430), DENY(431),
        DENY(432), DENY(433), DENY(442),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program = { sizeof(filter) / sizeof(filter[0]), filter };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
        prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program)) return 2;
    errno = 0;
    if (syscall(SYS_mount, "none", argv[1], "tmpfs", 0, 0) != -1 || errno != EPERM) return 3;
    errno = 0;
    if (syscall(SYS_mknodat, AT_FDCWD, argv[2], 0020666, 0) != -1 || errno != EPERM) return 4;
    errno = 0;
    if (open(argv[2], O_WRONLY) != -1 || errno != EACCES) return 5;
    int fd = open(argv[3], O_RDWR);
    char value;
    if (fd < 0 || write(fd, "X", 1) != 1 || lseek(fd, 0, SEEK_SET) != 0 ||
        read(fd, &value, 1) != 1 || value != 'X') return 6;
    close(fd);
    puts("NODEV_PROBE_OK");
    return 0;
}
