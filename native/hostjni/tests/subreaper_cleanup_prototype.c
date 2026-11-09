#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef P_PIDFD
#define P_PIDFD 3
#endif

/* Test-only supervisor. Single thread, SIGCHLD default, complete original
 * ancestry, no other reaper. ECHILD is not proof for an unknown old VM. */
int test_reap_owned_children(void) {
    for (int pass = 0; pass < 128; ++pass) {
        siginfo_t si = {0};
        if (waitid(P_ALL, 0, &si, WEXITED | WNOHANG | WNOWAIT) < 0) {
            if (errno == ECHILD) return 0;
            fprintf(stderr, "wait_all_errno=%d\n", errno); return -1;
        }
        DIR *proc = opendir("/proc");
        if (!proc) return -1;
        int selected = -1; struct dirent *entry;
        while ((entry = readdir(proc)) != NULL) {
            const char *n = entry->d_name;
            if (!isdigit((unsigned char)*n)) continue;
            char *end; long number = strtol(n, &end, 10);
            if (*end || number <= 0 || number > INT_MAX) continue;
            char path[96];
            if (snprintf(path, sizeof path, "/proc/%ld", number) >= (int)sizeof path) continue;
            struct stat attrs;
            if (stat(path, &attrs) != 0 || attrs.st_uid != getuid()) continue;
            if (snprintf(path, sizeof path, "/proc/%ld/status", number) >= (int)sizeof path) continue;
            FILE *status = fopen(path, "re");
            if (!status) continue;
            char line[256]; long parent = -1;
            while (fgets(line, sizeof line, status)) if (sscanf(line, "PPid:%ld", &parent) == 1) break;
            fclose(status);
            if (parent != (long)getpid()) continue;
            int fd = (int)syscall(__NR_pidfd_open, (pid_t)number, 0);
            if (fd < 0) continue;
            siginfo_t check = {0};
            /* Kernel must confirm this pidfd currently denotes our child. */
            if (waitid(P_PIDFD, (id_t)fd, &check, WEXITED | WNOHANG | WNOWAIT) != 0) { close(fd); continue; }
            selected = fd; break;
        }
        closedir(proc);
        if (selected < 0) { fprintf(stderr, "owned_child_not_visible\n"); return -1; }
        int sent = (int)syscall(__NR_pidfd_send_signal, selected, SIGKILL, NULL, 0);
        if (sent != 0 && errno != ESRCH) { close(selected); return -1; }
        struct pollfd pfd = {selected, POLLIN, 0};
        int ready;
        do { ready = poll(&pfd, 1, 5000); } while (ready < 0 && errno == EINTR);
        if (ready != 1 || !(pfd.revents & POLLIN)) { close(selected); return -1; }
        siginfo_t reaped = {0};
        if (waitid(P_PIDFD, (id_t)selected, &reaped, WEXITED) != 0 || reaped.si_pid <= 0) { close(selected); return -1; }
        close(selected);
    }
    return -1;
}
