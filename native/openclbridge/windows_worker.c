#include "openclbridge.h"
#include "abi.h"

/* Win32 declarations used by this freestanding diagnostic executable. */
typedef void *HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
#define WIN_IMPORT __declspec(dllimport)
WIN_IMPORT HANDLE __stdcall GetStdHandle(DWORD);
WIN_IMPORT BOOL __stdcall ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
WIN_IMPORT BOOL __stdcall WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
WIN_IMPORT DWORD __stdcall GetLastError(void);
WIN_IMPORT HANDLE __stdcall GetProcessHeap(void);
WIN_IMPORT void *__stdcall HeapAlloc(HANDLE, DWORD, size_t);
WIN_IMPORT void *__stdcall MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, size_t);
WIN_IMPORT HANDLE __stdcall LoadLibraryExW(const wchar_t *, HANDLE, DWORD);
WIN_IMPORT intptr_t (__stdcall *__stdcall GetProcAddress(HANDLE, const char *))(void);
WIN_IMPORT __declspec(noreturn) void __stdcall ExitProcess(unsigned int);

enum { ERROR_BROKEN_PIPE = 109, FILE_MAP_WRITE = 2, LOAD_LIBRARY_SEARCH_SYSTEM32 = 0x800 };

struct pipes {
    HANDLE input;
    HANDLE output;
    int shared_reply;
};

static void *lookup(void *library, const char *name) {
    return (void *)GetProcAddress(library, name);
}

static int read_pipe(void *stream, void *buffer, uint32_t size) {
    struct pipes *pipes = stream;
    unsigned char *bytes = buffer;
    uint32_t remaining = size;
    while (remaining) {
        DWORD count;
        if (!ReadFile(pipes->input, bytes, remaining, &count, NULL)) {
            return remaining == size && GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
        }
        if (!count) return remaining == size ? 0 : -1;
        remaining -= count;
        bytes += count;
    }
    return 1;
}

static int write_pipe(void *stream, const void *buffer, uint32_t size) {
    struct pipes *pipes = stream;
    const unsigned char *bytes = buffer;
    if (pipes->shared_reply) size = RME_CL_HEADER_SIZE;
    while (size) {
        DWORD count;
        if (!WriteFile(pipes->output, bytes, size, &count, NULL) || !count) return -1;
        size -= count;
        bytes += count;
    }
    return 0;
}

static int read_abi(void *stream, void *buffer, size_t size) { return read_pipe(stream, buffer, (uint32_t)size); }
static int write_abi(void *stream, const struct rme_cl_reply *reply, const void *buffer, size_t size) {
    return write_pipe(stream, reply, sizeof(*reply)) || (size && write_pipe(stream, buffer, (uint32_t)size));
}

void mainCRTStartup(void) {
    struct pipes pipes = {GetStdHandle((DWORD)-10), GetStdHandle((DWORD)-11), 0};
    HANDLE mapping;
    if (read_pipe(&pipes, &mapping, sizeof(mapping)) != 1) ExitProcess(2);
    HANDLE library = LoadLibraryExW(L"OpenCL.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) ExitProcess(2);
    if ((intptr_t)mapping == -1) ExitProcess(rme_cl_abi_serve(&pipes, read_abi, write_abi, NULL, NULL, library, lookup) ? 3 : 0);
    pipes.shared_reply = mapping != NULL;

    HANDLE heap = GetProcessHeap();
    void *session = HeapAlloc(heap, 0, rme_cl_session_size());
    void *input = HeapAlloc(heap, 0, RME_CL_PAYLOAD_MAX);
    size_t output_size = RME_CL_HEADER_SIZE + RME_CL_PAYLOAD_MAX;
    void *output = mapping ? MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, output_size)
                           : HeapAlloc(heap, 0, output_size);
    if (!session || !input || !output || rme_cl_init(session, lookup, library)) ExitProcess(2);
    int status = rme_cl_serve(session, &pipes, read_pipe, write_pipe, input, output);
    rme_cl_destroy(session);
    ExitProcess(status ? 3 : 0);
}
