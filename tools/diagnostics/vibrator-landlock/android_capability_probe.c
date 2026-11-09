#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Read-only capability probe. No mount is attached or changed. */
#ifndef __NR_open_tree
#if defined(__aarch64__) || defined(__x86_64__)
#define __NR_open_tree 428
#else
#error Unsupported architecture
#endif
#endif
#ifndef OPEN_TREE_CLONE
#define OPEN_TREE_CLONE 1U
#endif
#ifndef OPEN_TREE_CLOEXEC
#define OPEN_TREE_CLOEXEC O_CLOEXEC
#endif

static const char clone_files[] = "/data/user/0/app.redmagicemu.clone/files";

static void print_file(const char *label, const char *path) {
    FILE *f = fopen(path, "re");
    if (!f) {
        printf("%s unavailable errno=%d\n", label, errno);
        return;
    }
    char line[256];
    if (fgets(line, sizeof(line), f)) printf("%s %s%s", label, line, strchr(line, '\n') ? "" : "\n");
    else printf("%s unavailable errno=%d\n", label, ferror(f) ? errno : 0);
    fclose(f);
}

static void print_seccomp(void) {
    FILE *f = fopen("/proc/self/status", "re");
    if (!f) { printf("status unavailable errno=%d\n", errno); return; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "Seccomp:", 8) || !strncmp(line, "Seccomp_filters:", 16) ||
            !strncmp(line, "NoNewPrivs:", 11)) fputs(line, stdout);
    }
    fclose(f);
}

static void child_probe(void) {
    alarm(5);
    int ns = open("/proc/self/ns/mnt", O_RDONLY | O_CLOEXEC);
    struct stat before, after;
    int before_ok = ns >= 0 && fstat(ns, &before) == 0;
    if (!before_ok) {
        printf("mount_namespace_before failed errno=%d\n", errno);
        if (ns >= 0) close(ns);
        ns = -1;
        fflush(stdout);
    }
    if (unshare(CLONE_NEWUSER | CLONE_NEWNS)) {
        printf("unshare failed errno=%d\n", errno);
        if (ns >= 0) close(ns);
        fflush(stdout);
        _exit(1);
    }
    if (!before_ok) {
        puts("unshare ok mount_namespace_unverified; no further checks");
        fflush(stdout);
        _exit(1);
    }
    if (stat("/proc/self/ns/mnt", &after)) {
        printf("mount_namespace_after failed errno=%d\n", errno);
        close(ns);
        fflush(stdout);
        _exit(1);
    }
    close(ns);
    if (before.st_dev == after.st_dev && before.st_ino == after.st_ino) {
        puts("mount_namespace unchanged");
        fflush(stdout);
        _exit(1);
    }
    puts("unshare ok mount_namespace_changed");
    /* An unattached, non-recursive clone is discarded when closed. It is not
     * attached to any path, and does not change the original mount table. */
    int tree = (int)syscall(__NR_open_tree, AT_FDCWD, "/", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
    if (tree < 0) printf("open_tree_clone failed errno=%d\n", errno);
    else { puts("open_tree_clone ok"); close(tree); }
    fflush(stdout);
    _exit(tree < 0 ? 1 : 0);
}

static void single_unshare_probe(int flag) {
    alarm(5);
    if (unshare(flag)) { printf("unshare_single failed errno=%d\n", errno); fflush(stdout); _exit(1); }
    puts("unshare_single ok");
    fflush(stdout);
    _exit(0);
}

static void files_tree_probe(void) {
    alarm(5);
    /* Clone is never attached, and close discards the detached mount. */
    int tree = (int)syscall(__NR_open_tree, AT_FDCWD, clone_files, OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC);
    if (tree < 0) { printf("files_open_tree_clone failed errno=%d\n", errno); fflush(stdout); _exit(1); }
    close(tree);
    puts("files_open_tree_clone ok");
    fflush(stdout);
    _exit(0);
}

static void print_fd_reopen(void) {
    int fd = open(clone_files, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) { printf("clone_files_open failed errno=%d\n", errno); return; }
    char path[64];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    int reopened = open(path, O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (reopened < 0) printf("clone_files_fd_reopen failed errno=%d\n", errno);
    else {
        struct stat original, copy;
        if (fstat(fd, &original) || fstat(reopened, &copy)) printf("clone_files_fd_stat failed errno=%d\n", errno);
        else printf("clone_files_fd_reopen same=%d\n", original.st_dev == copy.st_dev && original.st_ino == copy.st_ino);
        close(reopened);
    }
    close(fd);
}

static int run_probe(const char *label, int mode) {
    fflush(stdout);
    pid_t child = fork();
    if (child < 0) { printf("%s fork failed errno=%d\n", label, errno); return 1; }
    if (child == 0) {
        if (mode == 0) child_probe();
        if (mode == 1) single_unshare_probe(CLONE_NEWUSER);
        if (mode == 2) single_unshare_probe(CLONE_NEWNS);
        files_tree_probe();
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        printf("%s waitpid failed errno=%d\n", label, errno);
        return 1;
    }
    if (WIFSIGNALED(status)) { printf("%s child signal=%d\n", label, WTERMSIG(status)); return 1; }
    if (!WIFEXITED(status)) { printf("%s child unexpected_status\n", label); return 1; }
    printf("%s child exit=%d\n", label, WEXITSTATUS(status));
    return WEXITSTATUS(status) != 0;
}

int main(void) {
    printf("uid=%lu gid=%lu\n", (unsigned long)getuid(), (unsigned long)getgid());
    print_file("selinux", "/proc/self/attr/current");
    print_seccomp();
    int mounts = open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC);
    if (mounts < 0) printf("mountinfo unavailable errno=%d\n", errno);
    else { puts("mountinfo readable"); close(mounts); }
    struct statvfs fs;
    if (statvfs(clone_files, &fs))
        printf("clone_files_statvfs failed errno=%d\n", errno);
    else printf("clone_files_nodev=%d\n", !!(fs.f_flag & ST_NODEV));
    print_fd_reopen();
    int failed = 0;
    failed |= run_probe("combined", 0);
    failed |= run_probe("user_namespace", 1);
    failed |= run_probe("mount_namespace", 2);
    failed |= run_probe("files_tree", 3);
    return failed ? 1 : 0;
}
