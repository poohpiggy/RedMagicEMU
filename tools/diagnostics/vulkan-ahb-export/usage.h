#ifndef RME_AHB_EXPORT_USAGE_H
#define RME_AHB_EXPORT_USAGE_H
#include <stdint.h>
#include <android/hardware_buffer.h>
static inline uint64_t cpu_read(uint64_t usage) {
    uint64_t mode=usage & AHARDWAREBUFFER_USAGE_CPU_READ_MASK;
    if(mode==AHARDWAREBUFFER_USAGE_CPU_READ_RARELY || mode==AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN) return mode;
    return 0;
}
static inline uint64_t cpu_write(uint64_t usage) {
    uint64_t mode=usage & AHARDWAREBUFFER_USAGE_CPU_WRITE_MASK;
    if(mode==AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY || mode==AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN) return mode;
    return 0;
}
#endif