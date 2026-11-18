/* Test ICD wrapper. Leave writes pending until the consumer sends clFinish. */
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#include <dlfcn.h>
#include <stdio.h>

static cl_event gate;

static cl_int upload_dependencies(cl_command_queue queue, cl_uint count, const cl_event *wait, cl_event *dependencies) {
    if (count > 1) return CL_INVALID_EVENT_WAIT_LIST;
    if (!gate) {
        cl_context context;
        cl_int status = clGetCommandQueueInfo(queue, CL_QUEUE_CONTEXT, sizeof(context), &context, NULL);
        if (status) return status;
        gate = clCreateUserEvent(context, &status);
        if (status) return status;
    }
    dependencies[0] = gate;
    if (count == 1) dependencies[1] = wait[0];
    return CL_SUCCESS;
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBuffer(cl_command_queue queue, cl_mem memory, cl_bool blocking,
        size_t offset, size_t size, const void *data, cl_uint count, const cl_event *wait, cl_event *event) {
    if (blocking) {
        fputs("Guest nonblocking upload reached the driver as blocking\n", stderr);
        return CL_INVALID_OPERATION;
    }
    cl_event dependencies[2];
    cl_int status = upload_dependencies(queue, count, wait, dependencies);
    if (status) return status;
    __typeof__(&clEnqueueWriteBuffer) enqueue = dlsym(RTLD_NEXT, "clEnqueueWriteBuffer");
    return enqueue(queue, memory, blocking, offset, size, data, count + 1, dependencies, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteImage(cl_command_queue queue, cl_mem image, cl_bool blocking,
        const size_t *origin, const size_t *region, size_t row_pitch, size_t slice_pitch, const void *data,
        cl_uint count, const cl_event *wait, cl_event *event) {
    if (blocking) {
        fputs("Guest nonblocking image upload reached the driver as blocking\n", stderr);
        return CL_INVALID_OPERATION;
    }
    cl_event dependencies[2];
    cl_int status = upload_dependencies(queue, count, wait, dependencies);
    if (status) return status;
    __typeof__(&clEnqueueWriteImage) enqueue = dlsym(RTLD_NEXT, "clEnqueueWriteImage");
    return enqueue(queue, image, blocking, origin, region, row_pitch, slice_pitch, data,
                   count + 1, dependencies, event);
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueWriteBufferRect(cl_command_queue queue, cl_mem buffer, cl_bool blocking,
        const size_t *origin, const size_t *host_origin, const size_t *region,
        size_t row, size_t slice, size_t host_row, size_t host_slice, const void *data,
        cl_uint count, const cl_event *wait, cl_event *event) {
    if (blocking) {
        fputs("Guest nonblocking rectangular upload reached the driver as blocking\n", stderr);
        return CL_INVALID_OPERATION;
    }
    cl_event dependencies[2];
    cl_int status = upload_dependencies(queue, count, wait, dependencies);
    if (status) return status;
    __typeof__(&clEnqueueWriteBufferRect) enqueue = dlsym(RTLD_NEXT, "clEnqueueWriteBufferRect");
    return enqueue(queue, buffer, blocking, origin, host_origin, region, row, slice, host_row, host_slice,
                   data, count + 1, dependencies, event);
}

CL_API_ENTRY cl_int CL_API_CALL clFinish(cl_command_queue queue) {
    if (gate) {
        cl_int status = clSetUserEventStatus(gate, CL_COMPLETE);
        clReleaseEvent(gate);
        gate = NULL;
        if (status) return status;
    }
    __typeof__(&clFinish) finish = dlsym(RTLD_NEXT, "clFinish");
    return finish(queue);
}
