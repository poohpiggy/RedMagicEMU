#ifndef RME_GUEST_PROCESS_IDENTITY_H
#define RME_GUEST_PROCESS_IDENTITY_H

#include <charconv>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace rme {

struct GuestIdentity {
    const std::string rootEnvironment;
    const struct stat runner;
    const pid_t parent;

    static pid_t parentOf(pid_t pid) {
        std::ifstream status("/proc/" + std::to_string(pid) + "/status");
        for (std::string line; std::getline(status, line);) {
            if (line.compare(0, 5, "PPid:") != 0) continue;
            const auto start = line.find_first_not_of(" \t", 5);
            pid_t parent = -1;
            if (start != std::string::npos)
                std::from_chars(line.data() + start, line.data() + line.size(), parent);
            return parent;
        }
        return -1;
    }

    bool owns(pid_t pid) const {
        const std::string proc = "/proc/" + std::to_string(pid) + '/';
        struct stat exe{};
        if (stat((proc + "exe").c_str(), &exe) || exe.st_dev != runner.st_dev ||
            exe.st_ino != runner.st_ino) return false;
        for (pid_t ancestor = parentOf(pid); ancestor > 1; ancestor = parentOf(ancestor)) {
            if (ancestor != parent) continue;
            std::ifstream environment(proc + "environ", std::ios::binary);
            for (std::string entry; std::getline(environment, entry, '\0');) {
                if (entry == rootEnvironment) return true;
            }
            break;
        }
        return false;
    }

    static bool authorize(pid_t pid, uid_t uid, gid_t, void* context) {
        return uid == getuid() && static_cast<const GuestIdentity*>(context)->owns(pid);
    }
};

}  // namespace rme

#endif
