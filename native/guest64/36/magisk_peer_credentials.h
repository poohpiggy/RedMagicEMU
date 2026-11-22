/* Magisk peers use the executable and initial environment of the Linux process
 * attached to the socket. The ordinary Guest UID compatibility files are not
 * authoritative for Magisk. */

static int magisk_parse_id(const char **input, u32 *result) {
    const char *p = *input;
    if (*p < '0' || *p > '9') return 0;
    u64 value = 0;
    do {
        value = value * 10 + (unsigned)(*p++ - '0');
        if (value > 0xffffffffU) return 0;
    } while (*p >= '0' && *p <= '9');
    *input = p;
    *result = (u32)value;
    return 1;
}

static int magisk_read_peer_environment(long procfd, u32 *uid, u32 *gid) {
    char env[8192];
    long fd = S4(NR_openat, procfd, "environ", 0x80000 /* O_CLOEXEC */, 0);
    if (fd < 0) return 0;
    u64 size = 0;
    long count;
    do {
        count = S3(NR_read, fd, env + size, sizeof env - size);
        if (count > 0) size += (u64)count;
    } while (count > 0 && size < sizeof env);
    S1(NR_close, fd);
    if (count != 0 || !size || env[size - 1] != '\0') return 0;

    int root_seen = 0, id_seen = 0;
    for (const char *e = env; e < env + size; e += slen(e) + 1) {
        if (pre(e, "AEMU64_ROOT=")) {
            if (root_seen || !seq(e + sizeof "AEMU64_ROOT=" - 1, g_root)) return 0;
            root_seen = 1;
        }
        if (pre(e, "AEMU64_ID=")) {
            const char *value = e + sizeof "AEMU64_ID=" - 1;
            if (id_seen || !magisk_parse_id(&value, uid) || *value++ != ',' ||
                !magisk_parse_id(&value, gid) || *value != '\0') return 0;
            id_seen = 1;
        }
    }
    return root_seen && id_seen;
}

static long magisk_peer_credentials(u32 *cred) {
    if (!cred[0] || cred[1] != g_real_uid) return -EACCES;

    char proc[32] = "/proc/", digits[24];
    const char *pid = utoa(cred[0], digits + sizeof digits);
    memcpy(proc + 6, pid, slen(pid) + 1);
    long procfd = S4(NR_openat, AT_FDCWD, proc, 0x290000 /* O_PATH | O_DIRECTORY | O_CLOEXEC */, 0);
    if (procfd < 0) return -EACCES;

    struct kstat peer, self;
    u32 uid = 0, gid = 0;
    int valid = S4(NR_newfstatat, procfd, "exe", &peer, 0) == 0 &&
        S4(NR_newfstatat, AT_FDCWD, "/proc/self/exe", &self, 0) == 0 &&
        peer.st_dev == self.st_dev && peer.st_ino == self.st_ino &&
        magisk_read_peer_environment(procfd, &uid, &gid);
    S1(NR_close, procfd);
    if (!valid) return -EACCES;

    cred[1] = uid;
    cred[2] = gid;
    return 0;
}
