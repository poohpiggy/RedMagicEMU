#include <stdint.h>
#include <stdio.h>
#include <android/hardware_buffer.h>
#include "usage.h"
int main(void) {
    uint64_t r=AHARDWAREBUFFER_USAGE_CPU_READ_RARELY, R=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
    uint64_t w=AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY, W=AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    if (cpu_read(0) || cpu_write(0)) return 1;
    if (cpu_read(r|w)!=r || cpu_write(r|w)!=w) return 2;
    if (cpu_read(R|W)!=R || cpu_write(R|W)!=W) return 3;
    if (cpu_read(AHARDWAREBUFFER_USAGE_CPU_READ_MASK) || cpu_write(AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK)) return 4;
    puts("AHB_CPU_USAGE_TEST_OK"); return 0;
}