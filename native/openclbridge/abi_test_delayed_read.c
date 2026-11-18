/* Hold native readbacks pending until the consumer explicitly synchronizes. */
#include "abi.h"
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cl_event gate;
static unsigned pending_reads;

static int in_readback_mapping(const void *address) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), maps)) {
        unsigned long long start, end;
        if (sscanf(line, "%llx-%llx", &start, &end) == 2 &&
            (uintptr_t)address >= start && (uintptr_t)address < end && strstr(line, "memfd:rme-cl-readback")) {
            found = 1;
            break;
        }
    }
    fclose(maps);
    return found;
}

static cl_int open_gate(void) {
    if (!gate) return CL_SUCCESS;
    cl_int status = clSetUserEventStatus(gate, CL_COMPLETE);
    clReleaseEvent(gate);
    gate = NULL;
    pending_reads = 0;
    return status;
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueReadBuffer(cl_command_queue queue, cl_mem memory, cl_bool blocking,
        size_t offset, size_t bytes, void *output, cl_uint count, const cl_event *wait, cl_event *event) {
    if (blocking) {
        fputs("Guest nonblocking read reached the driver as blocking\n", stderr);
        return CL_INVALID_OPERATION;
    }
    if (getenv("RME_CL_EXPECT_READBACK_SLOTS") &&
        in_readback_mapping(output) != (pending_reads < RME_CL_READBACK_SLOT_COUNT)) {
        fputs("Driver read destination is outside the expected readback storage\n", stderr);
        return CL_INVALID_OPERATION;
    }
    if (!gate) {
        cl_context context;
        cl_int status = clGetCommandQueueInfo(queue, CL_QUEUE_CONTEXT, sizeof(context), &context, NULL);
        if (status) return status;
        gate = clCreateUserEvent(context, &status);
        if (status) return status;
    }
    cl_event dependencies[2] = {gate};
    if (count == 1) dependencies[1] = wait[0];
    else if (count) return CL_INVALID_EVENT_WAIT_LIST;
    __typeof__(&clEnqueueReadBuffer) read_buffer = dlsym(RTLD_NEXT, "clEnqueueReadBuffer");
    cl_int status = read_buffer(queue, memory, blocking, offset, bytes, output, count + 1, dependencies, event);
    if (!status) ++pending_reads;
    return status;
}

CL_API_ENTRY cl_int CL_API_CALL clWaitForEvents(cl_uint count, const cl_event *events) {
    cl_int status = open_gate();
    if (status) return status;
    __typeof__(&clWaitForEvents) wait = dlsym(RTLD_NEXT, "clWaitForEvents");
    return wait(count, events);
}

CL_API_ENTRY cl_int CL_API_CALL clFinish(cl_command_queue queue) {
    cl_int status = open_gate();
    if (status) return status;
    __typeof__(&clFinish) finish = dlsym(RTLD_NEXT, "clFinish");
    return finish(queue);
}

CL_API_ENTRY cl_int CL_API_CALL clFlush(cl_command_queue queue) {
    cl_int status = open_gate();
    if (status) return status;
    __typeof__(&clFlush) flush = dlsym(RTLD_NEXT, "clFlush");
    return flush(queue);
}
