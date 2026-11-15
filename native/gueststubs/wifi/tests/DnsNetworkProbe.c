/* Run in the target Guest: DnsNetworkProbe <wifi-netid> [bypass].
 * Compares network-bound getaddrinfo with Android's raw DNS API for one
 * public host. Outputs counts and return codes, never resolved addresses.
 * The optional flag exercises NetworkMonitor's private-DNS bypass netId.
 */
#include <android/multinetwork.h>
#include <arpa/nameser.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) return 2;
    char *end = NULL;
    unsigned long id = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end || id == 0 || id > 65535) return 2;
    if (argc == 3) {
        if (strcmp(argv[2], "bypass") != 0) return 2;
        id |= 0x80000000UL;
    }
    /* Matches Android Network.getNetworkHandle, including its resolver bit. */
    net_handle_t network = ((net_handle_t)id << 32) | 0xcafed00dULL;
    alarm(30);
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = AF_INET;
    int rc = android_getaddrinfofornetwork(network, "www.baidu.com", NULL, &hints, &result);
    int count = 0;
    for (struct addrinfo *entry = result; entry != NULL; entry = entry->ai_next) ++count;
    if (result) freeaddrinfo(result);
    printf("GETADDRINFO_RC=%d COUNT=%d\n", rc, count);
    fflush(stdout);

    int fd = android_res_nquery(network, "www.baidu.com", ns_c_in, ns_t_a, 0);
    if (fd < 0) {
        printf("RAW_QUERY_OPEN_RC=%d\n", fd);
        return 1;
    }
    struct pollfd pollfd = {.fd = fd, .events = POLLIN};
    int ready = poll(&pollfd, 1, 12000);
    if (ready <= 0) {
        android_res_cancel(fd);
        printf("RAW_POLL_RC=%d\n", ready);
        return 1;
    }
    unsigned char answer[4096];
    int rcode = -1;
    int size = android_res_nresult(fd, &rcode, answer, sizeof(answer));
    int qr = size >= 12 ? (answer[2] & 0x80) != 0 : -1;
    int answers = size >= 12 ? (answer[6] << 8) | answer[7] : -1;
    printf("RAW_RESULT_RC=%d RCODE=%d QR=%d ANSWERS=%d\n", size, rcode, qr, answers);
    return rc == 0 && count > 0 && size >= 12 && rcode == 0 && qr == 1 && answers > 0 ? 0 : 1;
}
