#define _GNU_SOURCE
#include "parentage.h"
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int read_file(const char *path, char *output, size_t capacity) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return 0;
    ssize_t count = read(fd, output, capacity - 1);
    close(fd);
    if (count <= 0 || (size_t)count == capacity - 1) return 0;
    output[count] = 0;
    return 1;
}

static int number_field(const char *data, const char *key, long expected) {
    size_t length = strlen(key);
    for (const char *line = data; line && *line;) {
        if (strncmp(line, key, length) == 0 && line[length] == ':') {
            char *end = NULL;
            long value = strtol(line + length + 1, &end, 10);
            while (end && (*end == ' ' || *end == '\t')) ++end;
            return end && (*end == '\n' || *end == '\0') && value == expected;
        }
        line = strchr(line, '\n');
        if (line) ++line;
    }
    return 0;
}

static int live_pidfd(int fd, pid_t pid) {
    if (fd < 0 || pid <= 0) return 0;
    struct pollfd item = { .fd = fd, .events = POLLIN };
    if (poll(&item, 1, 0) != 0) return 0;
    char path[64], descriptor[64], data[2048];
    if (snprintf(path, sizeof path, "/proc/self/fd/%d", fd) >= (int)sizeof path) return 0;
    ssize_t size = readlink(path, descriptor, sizeof descriptor - 1);
    if (size <= 0 || (size_t)size >= sizeof descriptor - 1) return 0;
    descriptor[size] = 0;
    if (strcmp(descriptor, "anon_inode:[pidfd]")) return 0;
    if (snprintf(path, sizeof path, "/proc/self/fdinfo/%d", fd) >= (int)sizeof path) return 0;
    return read_file(path, data, sizeof data) && number_field(data, "Pid", pid);
}

int rme_observe_parentage(int parent_fd, pid_t parent_pid, pid_t sender_pid,
                          int child_fd, pid_t child_pid) {
    if (parent_pid <= 0 || child_pid <= 0 || parent_pid == child_pid || sender_pid != parent_pid ||
            !live_pidfd(parent_fd, parent_pid) || !live_pidfd(child_fd, child_pid)) return 0;
    char path[64], data[8192];
    if (snprintf(path, sizeof path, "/proc/%ld/status", (long)child_pid) >= (int)sizeof path) return 0;
    if (!read_file(path, data, sizeof data) || !number_field(data, "Pid", child_pid) ||
            !number_field(data, "Tgid", child_pid) || !number_field(data, "PPid", parent_pid)) return 0;
    return live_pidfd(parent_fd, parent_pid) && live_pidfd(child_fd, child_pid);
}
