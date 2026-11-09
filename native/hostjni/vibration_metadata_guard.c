#include "vibration_metadata_guard.h"
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

/* This filter is an isolated experiment for the target's ARM64 process, with
 * x86-64 support only for the self-created Linux regression test. */
#if defined(__aarch64__)
#define RME_AUDIT_ARCH AUDIT_ARCH_AARCH64
#elif defined(__x86_64__)
#define RME_AUDIT_ARCH AUDIT_ARCH_X86_64
#else
#error unsupported architecture for experimental vibration metadata filter
#endif

/* Older NDK headers may lack these numbers; all three are identical on the
 * supported ARM64 and x86-64 ABIs. */
#ifndef __NR_fchmodat2
#define __NR_fchmodat2 452
#endif
#ifndef __NR_setxattrat
#define __NR_setxattrat 463
#endif
#ifndef __NR_removexattrat
#define __NR_removexattrat 466
#endif

#define RME_DENY(nr) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)

int rme_vibration_metadata_restrict(void) {
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, RME_AUDIT_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
#if defined(__x86_64__)
        /* x32 syscall numbers share AUDIT_ARCH_X86_64; fail closed. */
        BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000U, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM),
#endif
#ifdef __NR_chmod
        RME_DENY(__NR_chmod),
#endif
#ifdef __NR_fchmod
        RME_DENY(__NR_fchmod),
#endif
#ifdef __NR_fchmodat
        RME_DENY(__NR_fchmodat),
#endif
        RME_DENY(__NR_fchmodat2),
#ifdef __NR_chown
        RME_DENY(__NR_chown),
#endif
#ifdef __NR_lchown
        RME_DENY(__NR_lchown),
#endif
#ifdef __NR_fchown
        RME_DENY(__NR_fchown),
#endif
#ifdef __NR_fchownat
        RME_DENY(__NR_fchownat),
#endif
#ifdef __NR_utime
        RME_DENY(__NR_utime),
#endif
#ifdef __NR_utimes
        RME_DENY(__NR_utimes),
#endif
#ifdef __NR_futimesat
        RME_DENY(__NR_futimesat),
#endif
#ifdef __NR_utimensat
        RME_DENY(__NR_utimensat),
#endif
#ifdef __NR_utimensat_time64
        RME_DENY(__NR_utimensat_time64),
#endif
#ifdef __NR_setxattr
        RME_DENY(__NR_setxattr),
#endif
#ifdef __NR_lsetxattr
        RME_DENY(__NR_lsetxattr),
#endif
#ifdef __NR_fsetxattr
        RME_DENY(__NR_fsetxattr),
#endif
        RME_DENY(__NR_setxattrat),
#ifdef __NR_removexattr
        RME_DENY(__NR_removexattr),
#endif
#ifdef __NR_lremovexattr
        RME_DENY(__NR_lremovexattr),
#endif
#ifdef __NR_fremovexattr
        RME_DENY(__NR_fremovexattr),
#endif
        RME_DENY(__NR_removexattrat),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) return -errno;
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) < 0) return -errno;
    return 0;
}
