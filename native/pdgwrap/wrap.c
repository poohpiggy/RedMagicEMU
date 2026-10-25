// pdgwrap <program> [args...]: runs a program the app sandbox's seccomp filter would kill (a static glibc/Go
// binary such as payload-dumper-go). The child runs under ptrace; a system call the filter refuses arrives as
// SIGSYS (SECCOMP_RET_TRAP), the call was not made, and here it returns -ENOSYS instead of killing the process,
// so the program takes its fallback. Each refused call is reported once on stderr.
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/elf.h>
#include <asm/ptrace.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: pdgwrap <program> [args]\n"); return 2; }
    pid_t c = fork();
    if (c < 0) { perror("fork"); return 2; }
    if (c == 0) {
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        raise(SIGSTOP);
        execv(argv[1], argv + 1);
        perror("exec"); _exit(127);
    }
    int st;
    waitpid(c, &st, 0);
    // follow the program's threads too: Go makes them with clone, and any of them may hit the filter
    ptrace(PTRACE_SETOPTIONS, c, 0, PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC | PTRACE_O_EXITKILL);
    ptrace(PTRACE_CONT, c, 0, 0);
    int seen[512] = {0};
    for (;;) {
        pid_t t = waitpid(-1, &st, __WALL);
        if (t < 0) { if (errno == EINTR) continue; break; }
        if (WIFEXITED(st) || WIFSIGNALED(st)) {
            if (t == c) return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
            continue;
        }
        if (!WIFSTOPPED(st)) continue;
        int sig = WSTOPSIG(st);
        if (st >> 16) sig = 0;                                     // a ptrace event (new thread)
        else if (sig == SIGSTOP) sig = 0;                          // a new thread's first stop
        else if (sig == SIGSYS) {
            siginfo_t si;
            if (ptrace(PTRACE_GETSIGINFO, t, 0, &si) == 0 && si.si_code == 1 /* SYS_SECCOMP */) {
                int nr = si.si_syscall;
                if (nr >= 0 && nr < 512 && !seen[nr]) { seen[nr] = 1; fprintf(stderr, "pdgwrap: syscall %d refused by the sandbox, returning ENOSYS\n", nr); }
                struct user_pt_regs r; struct iovec io = { &r, sizeof r };
                if (ptrace(PTRACE_GETREGSET, t, NT_PRSTATUS, &io) == 0) {
                    r.regs[0] = (unsigned long long)-ENOSYS;
                    ptrace(PTRACE_SETREGSET, t, NT_PRSTATUS, &io);
                }
                sig = 0;
            }
        }
        ptrace(PTRACE_CONT, t, 0, sig);
    }
    return 1;
}
