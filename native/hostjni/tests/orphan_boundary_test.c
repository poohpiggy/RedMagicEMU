#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Self-contained reproduction. The test process is a subreaper and only kills
 * its own recorded, unreaped descendant; no /proc scanning or VM is involved. */
static int line(int fd, char *out, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        struct pollfd p = {fd, POLLIN, 0};
        int ready;
        do { ready = poll(&p, 1, 5000); } while (ready < 0 && errno == EINTR);
        if (ready <= 0) return -1;
        char c;
        if (read(fd, &c, 1) != 1) return -1;
        if (c == '\n') { out[i] = 0; return 0; }
        out[i++] = c;
    }
    return -1;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--descendant") == 0) {
        int pdeath = -1, fd = atoi(argv[2]);
        if (prctl(PR_GET_PDEATHSIG, &pdeath) != 0) return 2;
        dprintf(fd, "DESC %ld %d %d\n", (long)getppid(), pdeath, getenv("AEMU64_ROOT") != NULL);
        for (;;) pause();
    }
    if (argc != 2 || (strcmp(argv[1], "--strict") && strcmp(argv[1], "--characterize"))) return 2;
    signal(SIGPIPE, SIG_IGN);
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0 || setenv("AEMU64_ROOT", "/synthetic/test-root", 1) != 0) return 2;
    int p[2], sync[2]; if (pipe(p) != 0 || pipe(sync) != 0) return 2;
    pid_t intermediate = fork();
    if (intermediate < 0) return 2;
    if (intermediate == 0) {
        close(p[0]);
        close(sync[1]);
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) _exit(3);
        pid_t descendant = fork();
        if (descendant < 0) _exit(3);
        if (descendant == 0) {
            char go;
            if (read(sync[0], &go, 1) != 1) _exit(3);
            close(sync[0]);
            if (setsid() < 0) _exit(3);
            char fd[24]; snprintf(fd, sizeof fd, "%d", p[1]);
            char *const args[] = {argv[0], "--descendant", fd, NULL};
            char *const env[] = {"PATH=/system/bin", NULL};
            execve(argv[0], args, env);
            _exit(3);
        }
        close(sync[0]);
        dprintf(p[1], "CHILD %ld\n", (long)descendant);
        _exit(0);
    }
    close(p[1]); close(sync[0]);
    char first[100], second[100]; long descendant = -1, parent = -1; int death = -1, marker = -1, status;
    int ok = line(p[0], first, sizeof first) == 0 && sscanf(first, "CHILD %ld", &descendant) == 1 && descendant > 0;
    if (waitpid(intermediate, &status, 0) != intermediate || !WIFEXITED(status) || WEXITSTATUS(status)) ok = 0;
    if (ok && write(sync[1], "G", 1) != 1) ok = 0;
    close(sync[1]);
    if (ok && (line(p[0], second, sizeof second) != 0 || sscanf(second, "DESC %ld %d %d", &parent, &death, &marker) != 3)) ok = 0;
    close(p[0]);
    int survived = ok && parent == (long)getpid() && death == 0 && marker == 0;
    if (descendant > 0) {
        /* It is our child and has not been reaped, so its PID cannot be reused. */
        kill((pid_t)descendant, SIGKILL);
        if (waitpid((pid_t)descendant, &status, 0) != (pid_t)descendant) ok = 0;
    } else {
        /* Without a received PID, closing sync forces the waiting child to exit. */
        while (waitpid(-1, &status, 0) < 0 && errno == EINTR) { }
    }
    if (!ok) { puts("ORPHAN_PROBE_ERROR"); return 2; }
    printf("DESCENDANT_SURVIVES_PARENT=%d PDEATHSIG_INHERITED=%d ROOT_MARKER_PRESENT=%d CLEANUP=OK\n", survived, death != 0, marker);
    return survived && strcmp(argv[1], "--strict") == 0 ? 1 : survived ? 0 : 2;
}
