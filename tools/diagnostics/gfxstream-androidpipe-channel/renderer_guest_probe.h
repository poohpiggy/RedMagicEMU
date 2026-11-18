#pragma once

#include <sys/types.h>

namespace rme {
int runGuestDeviceProbe(const char* root, const char* runner, const char* program,
                       const char* guestLibraryDir, const char* probeMode);
int runGuestApp(const char* root, pid_t pid, int readyFd);
int runGuest(const char* root, const char* runner, pid_t parent, int readyFd);
}
