/* Exercise a renderer-owned graphics allocation under the unmodified N64 runner. */
#include <errno.h>
#include <linux/dma-buf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#define WORDS 64
#define BYTES (WORDS * sizeof(uint32_t))

static int syncBuffer(int fd, uint64_t flags) {
    struct dma_buf_sync sync = {.flags = flags};
    int result;
    do {
        result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
    } while (result < 0 && (errno == EINTR || errno == EAGAIN));
    return result;
}

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    int peer = atoi(argv[1]);
    char code;
    struct iovec data = {.iov_base = &code, .iov_len = sizeof(code)};
    union {
        struct cmsghdr header;
        char bytes[CMSG_SPACE(sizeof(int))];
    } control;
    struct msghdr reply = {
        .msg_iov = &data,
        .msg_iovlen = 1,
        .msg_control = control.bytes,
        .msg_controllen = sizeof(control.bytes),
    };
    if (recvmsg(peer, &reply, MSG_CMSG_CLOEXEC) != 1 || code != 'M' ||
        (reply.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) return 2;
    struct cmsghdr *header = CMSG_FIRSTHDR(&reply);
    if (!header || header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len != CMSG_LEN(sizeof(int))) return 3;
    int fd;
    memcpy(&fd, CMSG_DATA(header), sizeof(fd));
    uint32_t *words = mmap(NULL, BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (words == MAP_FAILED) { perror("Guest mmap DMA buffer"); return 4; }
    if (syncBuffer(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE) != 0) {
        perror("Guest begin DMA buffer write");
        return 5;
    }
    for (size_t i = 0; i < WORDS; ++i) words[i] = (uint32_t)(i * 11 + 5);
    if (syncBuffer(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE) != 0) {
        perror("Guest end DMA buffer write");
        return 6;
    }
    if (write(peer, "W", 1) != 1 || read(peer, &code, 1) != 1 || code != 'R') return 7;
    if (syncBuffer(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ) != 0) return 8;
    size_t mismatches = 0;
    for (size_t i = 0; i < WORDS; ++i) mismatches += words[i] != UINT32_C(0x51a2d9f0);
    if (syncBuffer(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ) != 0) return 9;
    munmap(words, BYTES);
    close(fd);
    close(peer);
    if (mismatches) {
        fprintf(stderr, "GUEST_VULKAN_SHARED_MEMORY_FAILED mismatches=%zu\n", mismatches);
        return 10;
    }
    printf("GUEST_VULKAN_SHARED_MEMORY_OK values=%d\n", WORDS);
    return 0;
}
