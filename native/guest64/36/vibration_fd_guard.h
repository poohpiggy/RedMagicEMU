#ifndef RME_VIBRATION_FD_GUARD_H
#define RME_VIBRATION_FD_GUARD_H
/* Applied only after a successful recvmsg in the experimental vibrator
 * process. SCM_CREDENTIALS carries no descriptors and remains available for
 * the host handshake. Reject truncated controls and close any descriptors
 * installed by SCM_RIGHTS (or Linux SCM_PIDFD) before reporting failure.
 * This is not a complete hostile-process isolation boundary: callers share
 * memory/FD tables and other descriptor acquisition paths must be handled. */
#ifndef VIBRATION_CLOSE
#define VIBRATION_CLOSE(fd) S1(NR_close, (fd))
#endif
static int rme_vibration_check_received(struct msgh *message) {
    if (!message) return -1;
    int rejected = (message->flags & 0x08 /* MSG_CTRUNC */) != 0;
    u64 offset = 0, length = message->controllen;
    u8 *bytes = (u8 *)message->control;
    if (!bytes && length) rejected = 1;
    while (bytes && offset < length) {
        u64 remaining = length - offset;
        if (remaining < sizeof(struct cmsg)) { rejected = 1; break; }
        struct cmsg header;
        memcpy(&header, bytes + offset, sizeof header);
        if (header.len < sizeof header) { rejected = 1; break; }
        u64 available = header.len < remaining ? header.len : remaining;
        if (header.level == 1 /* SOL_SOCKET */ &&
            (header.type == 1 /* SCM_RIGHTS */ || header.type == 4 /* SCM_PIDFD */)) {
            rejected = 1;
            for (u64 p = sizeof header; p + sizeof(int) <= available; p += sizeof(int)) {
                int fd;
                memcpy(&fd, bytes + offset + p, sizeof fd);
                if (fd >= 0) (void)VIBRATION_CLOSE(fd);
            }
            if ((available - sizeof header) % sizeof(int)) rejected = 1;
        } else if (header.level != 1 || header.type != 2 /* SCM_CREDENTIALS */) {
            /* Unknown ancillary messages are not needed by this bridge. */
            rejected = 1;
        }
        if (header.len > remaining || header.len > ~(u64)0 - 7) { rejected = 1; break; }
        u64 step = (header.len + 7) & ~(u64)7;
        if (step > remaining) {
            if (header.len != remaining) rejected = 1;
            break;
        }
        offset += step;
    }
    if (!rejected) return 0;
    message->controllen = 0;
    message->flags |= 0x08 /* MSG_CTRUNC */;
    return -1; /* EPERM */
}
#endif
