/* Reuse the standalone diagnostic without changing its Vulkan work or cleanup. */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static atomic_flag active = ATOMIC_FLAG_INIT;
static atomic_bool completion_unknown;
static _Thread_local char *output_buffer;
static _Thread_local size_t output_size;
static _Thread_local size_t output_used;
static _Thread_local int saw_unknown_completion;

static int collect(const char *format, ...) {
    if (strstr(format, "QUEUE_COMPLETION_UNKNOWN")) saw_unknown_completion = 1;
    if (output_used >= output_size) return 0;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(output_buffer + output_used, output_size - output_used, format, args);
    va_end(args);
    if (written > 0) {
        size_t available = output_size - output_used;
        output_used += (size_t)written < available ? (size_t)written : available - 1;
    }
    return written;
}

#define printf(...) collect(__VA_ARGS__)
#define fprintf(stream, ...) collect(__VA_ARGS__)
#define puts(s) collect("%s\n", (s))
#define main rme_standalone_vulkan_probe_main
#include "../../tools/diagnostics/vulkan-compute/probe.c"
#undef main
#undef puts
#undef fprintf
#undef printf

int vulkan_host_probe(char *result, size_t size, int bad_expectation) {
    if (!result || !size) return -1;
    result[0] = '\0';
    if (atomic_flag_test_and_set(&active)) {
        snprintf(result, size, "FAIL Vulkan probe already running\n");
        return -1;
    }
    if (atomic_load(&completion_unknown)) {
        snprintf(result, size, "FAIL Vulkan queue completion was not established; restart process before retry\n");
        atomic_flag_clear(&active);
        return -1;
    }
    output_buffer = result;
    /* Reserve space for a final status even if a driver reports many devices. */
    output_size = size > 128 ? size - 128 : size;
    output_used = 0;
    saw_unknown_completion = 0;
    char *args[] = { "vulkan-probe", "--expect-mismatch", NULL };
    int rc = rme_standalone_vulkan_probe_main(bad_expectation ? 2 : 1, args);
    if (saw_unknown_completion) atomic_store(&completion_unknown, 1);
    if (size > 128) snprintf(result + output_used, size - output_used,
                             "\nPROBE_STATUS=%s%s\n", rc == 0 ? "OK" : "FAIL",
                             saw_unknown_completion ? " queue_completion_unknown" : "");
    output_buffer = NULL;
    output_size = output_used = 0;
    atomic_flag_clear(&active);
    return rc == 0 ? 0 : -1;
}
