#include "abi.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    void *driver = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!driver) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(argv[2]) >= sizeof(address.sun_path)) return 2;
    strcpy(address.sun_path, argv[2]);
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0 || bind(listener, (const struct sockaddr *)&address, sizeof(address)) || listen(listener, 1)) {
        perror("OpenCL loopback host");
        return 1;
    }
    puts("READY");
    fflush(stdout);
    int client = accept(listener, NULL, NULL);
    int status = client < 0 || rme_cl_abi_serve_fd(client, driver) != 0;
    if (client >= 0) close(client);
    close(listener);
    unlink(address.sun_path);
    return status;
}
