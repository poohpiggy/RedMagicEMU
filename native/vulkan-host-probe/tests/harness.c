#include <stdio.h>
#include <string.h>

int vulkan_host_probe(char *result, size_t size, int bad_expectation);

int main(int argc, char **argv) {
    char result[8192];
    int bad = argc > 1 && !strcmp(argv[1], "mismatch");
    int status = vulkan_host_probe(result, sizeof(result), bad);
    printf("STATUS=%d\n%s", status, result);
    if (argc > 1 && !strcmp(argv[1], "repeat")) {
        status = vulkan_host_probe(result, sizeof(result), 0);
        printf("SECOND_STATUS=%d\n%s", status, result);
    }
    return status == 0 ? 0 : 1;
}
