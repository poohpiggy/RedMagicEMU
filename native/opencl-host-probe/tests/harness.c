#include <stdio.h>
#include <stddef.h>

int opencl_host_probe(char *result, size_t size);

int main(void) {
    char result[1024] = {0};
    int status = opencl_host_probe(result, sizeof(result));
    puts(result);
    return status == 0 ? 0 : 1;
}
