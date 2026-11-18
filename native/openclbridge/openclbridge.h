#ifndef RME_OPENCLBRIDGE_H
#define RME_OPENCLBRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define RME_CL_EXPORT __declspec(dllexport)
#else
#define RME_CL_EXPORT __attribute__((visibility("default")))
#endif

#define RME_CL_MAGIC UINT32_C(0x314c4352)
#define RME_CL_PAYLOAD_MAX (UINT32_C(1) << 20)
#define RME_CL_HEADER_SIZE 12

enum rme_cl_op {
    RME_CL_OPEN = 1,
    RME_CL_BUILD,
    RME_CL_ALLOC,
    RME_CL_WRITE,
    RME_CL_MEMORY_ARG,
    RME_CL_VALUE_ARG,
    RME_CL_RUN,
    RME_CL_READ,
    RME_CL_RELEASE,
    RME_CL_RUN_READ,
};

typedef void *(*rme_cl_resolve)(void *library, const char *name);
/* Read returns 1 for completion, 0 for clean EOF, -1 for failure. Write returns 0 on completion. */
typedef int (*rme_cl_read)(void *stream, void *buffer, uint32_t size);
typedef int (*rme_cl_write)(void *stream, const void *buffer, uint32_t size);

RME_CL_EXPORT size_t rme_cl_session_size(void);
RME_CL_EXPORT int rme_cl_init(void *session, rme_cl_resolve resolve, void *library);
RME_CL_EXPORT void rme_cl_destroy(void *session);
RME_CL_EXPORT int rme_cl_call(void *session, uint32_t op, const void *input, uint32_t size,
                             void *output, uint32_t capacity, uint32_t *written);
/* The caller provides RME_CL_PAYLOAD_MAX input bytes and header + payload bytes for the reply. */
RME_CL_EXPORT int rme_cl_serve(void *session, void *stream, rme_cl_read read, rme_cl_write write,
                              void *input, void *output);

#endif
