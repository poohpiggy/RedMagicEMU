#define _GNU_SOURCE
#include <errno.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PTRACE_GET_SYSCALL_INFO
#define PTRACE_GET_SYSCALL_INFO 0x420e
#endif
#ifndef PTRACE_O_EXITKILL
#define PTRACE_O_EXITKILL 0x00100000
#endif

/* This local description contains only the stable entry portion of the Linux
 * PTRACE_GET_SYSCALL_INFO ABI, and is not an authorization or UID registry. */
struct syscall_entry_info {
    uint8_t op, pad[3];
    uint32_t arch;
    uint64_t ip, sp, nr, args[6];
};

static volatile sig_atomic_t trapped;
static void trap_handler(int sig) { if (sig == SIGSYS) ++trapped; }

static int install_test_filter(void) {
    struct sock_filter code[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_setresuid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog filter = { .len = sizeof(code) / sizeof(code[0]), .filter = code };
    struct sigaction sa = {0};
    sa.sa_handler = trap_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSYS, &sa, NULL) || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) return -1;
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &filter);
}

static void controlled_parent(pid_t supervisor) {
    /* Ensure the one test child is not left stopped if its tracer exits before
     * PTRACE_O_EXITKILL has been installed. */
    if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0 || getppid() != supervisor) _exit(77);
    if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0) _exit(77);
    if (raise(SIGSTOP) != 0) _exit(2);
    if (install_test_filter() != 0) _exit(77);
    pid_t child = fork();
    if (child < 0) _exit(3);
    if (child == 0) {
        uid_t uid = getuid();
        /* Seccomp traps this call; it does not change the real UID. */
        (void)syscall(SYS_setresuid, uid, uid, uid);
        _exit(trapped == 1 ? 0 : 4);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) _exit(5);
    _exit(0);
}

static int resume(pid_t pid, int signal_number) {
    return ptrace(PTRACE_SYSCALL, pid, NULL, (void *)(intptr_t)signal_number) == 0;
}

int main(void) {
    /* The tracer and all traced tasks belong exclusively to this test. */
    alarm(15);
    pid_t supervisor = getpid();
    pid_t parent = fork();
    if (parent < 0) return 2;
    if (parent == 0) controlled_parent(supervisor);
    int status = 0;
    if (waitpid(parent, &status, 0) != parent) return 3;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 77) {
        puts("SUPERVISION_PROBE_UNAVAILABLE"); return 77;
    }
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP) return 4;
    long options = PTRACE_O_TRACEFORK | PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL;
    if (ptrace(PTRACE_SETOPTIONS, parent, NULL, (void *)options) != 0) {
        perror("ptrace options");
        /* parent is our still-unreaped, stopped direct child; no PID lookup. */
        kill(parent, SIGKILL);
        while (waitpid(parent, &status, 0) < 0 && errno == EINTR) {}
        return 5;
    }
    if (!resume(parent, 0)) return 6;
    pid_t child = -1;
    int parent_done = 0, child_done = 0, fork_seen = 0, uid_seen = 0, sysinfo_unavailable = 0;
    while (!parent_done || !child_done) {
        pid_t pid = waitpid(-1, &status, __WALL);
        if (pid < 0) { if (errno == EINTR) continue; return 7; }
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (pid == parent && WIFEXITED(status) && WEXITSTATUS(status) == 77) return 77;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 8;
            if (pid == parent) parent_done = 1;
            else if (pid == child) child_done = 1;
            else return 9;
            continue;
        }
        if (!WIFSTOPPED(status)) return 10;
        int signal_number = WSTOPSIG(status);
        unsigned event = (unsigned)status >> 16;
        if (pid == parent && signal_number == SIGTRAP && event == PTRACE_EVENT_FORK) {
            unsigned long value = 0;
            if (ptrace(PTRACE_GETEVENTMSG, parent, NULL, &value) != 0 || !value) return 11;
            child = (pid_t)value;
            fork_seen = 1;
            if (!resume(pid, 0)) return 12;
        } else if (signal_number == (SIGTRAP | 0x80)) {
            if (pid == child) {
                struct syscall_entry_info info = {0};
                long size = ptrace(PTRACE_GET_SYSCALL_INFO, pid, sizeof(info), &info);
                if (size < 0 && (errno == EINVAL || errno == EIO)) sysinfo_unavailable = 1;
                else if (size < 0) return 13;
                else if (info.op == 1 && info.nr == SYS_setresuid &&
                         info.args[0] == (uint64_t)getuid() && info.args[1] == (uint64_t)getuid() &&
                         info.args[2] == (uint64_t)getuid()) uid_seen = 1;
            }
            if (!resume(pid, 0)) return 14;
        } else {
            /* Deliver the seccomp trap to the test's own handler; suppress
             * ptrace-generated SIGSTOP/SIGTRAP, preserve other signals. */
            int deliver = signal_number == SIGSTOP || signal_number == SIGTRAP ? 0 : signal_number;
            if (!resume(pid, deliver)) return 15;
        }
    }
    if (sysinfo_unavailable) { puts("SUPERVISION_SYSCALL_INFO_UNAVAILABLE"); return 77; }
    if (!fork_seen || !uid_seen) return 16;
    puts("SUPERVISION_PROBE_OK");
    return 0;
}
