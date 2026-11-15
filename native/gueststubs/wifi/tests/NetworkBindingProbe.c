/* Run inside the target Guest: NetworkBindingProbe <wifi-netid>.
 *
 * android_setsocknetwork() returning success is not sufficient evidence that
 * the socket was actually marked: the current guest engine can suppress an
 * SO_MARK error. Verify the kernel mark on the same socket immediately after
 * the Android API returns. No DNS, connection, or physical Wi-Fi changes.
 *
 * This is a diagnostic, not an authorization or a connectivity test. A match
 * alone does not prove that the underlying host network matches the guest.
 */
#include <android/multinetwork.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef SO_MARK
#define SO_MARK 36
#endif

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char *end = NULL;
    unsigned long netid = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end || netid == 0 || netid > 65535) return 2;

    const net_handle_t handle = ((net_handle_t)netid << 32) | 0xcafed00dULL;
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        printf("SOCKET_ERROR=%d\n", errno);
        return 1;
    }
    errno = 0;
    int bind_result = android_setsocknetwork(handle, fd);
    int bind_errno = errno;
    uint32_t mark = 0;
    socklen_t marklen = sizeof(mark);
    errno = 0;
    int get_result = getsockopt(fd, SOL_SOCKET, SO_MARK, &mark, &marklen);
    int get_errno = errno;
    close(fd);

    printf("BIND_RC=%d BIND_ERRNO=%d GET_MARK_RC=%d GET_MARK_ERRNO=%d ",
           bind_result, bind_errno, get_result, get_errno);
    printf("NETID_MATCH=%d EXPLICIT=%d\n", (mark & 0xffffu) == netid,
           (mark & 0x10000u) != 0);
    return bind_result == 0 && get_result == 0 && marklen == sizeof(mark) &&
            (mark & 0xffffu) == netid && (mark & 0x10000u) != 0 ? 0 : 1;
}
