#include "device_rank.h"
#include <stdio.h>
int main(void) {
    if (device_rank(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) <= device_rank(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) ||
        device_rank(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) <= device_rank(VK_PHYSICAL_DEVICE_TYPE_OTHER) ||
        device_rank(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) <= device_rank(VK_PHYSICAL_DEVICE_TYPE_CPU)) {
        fputs("FAIL: physical GPU device types must be preferred\n", stderr);
        return 1;
    }
    puts("DEVICE_RANK_OK");
    return 0;
}
