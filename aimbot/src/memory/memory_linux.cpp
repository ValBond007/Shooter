// =============================================================================
//  Linux backend: process_vm_readv on the same machine.
//  Only used for development/testing of the reader on Linux.
// =============================================================================
#include "memory.h"

#include <cstdio>

#if defined(__linux__)
#  include <dirent.h>
#  include <sys/uio.h>
#  include <unistd.h>

#  include <algorithm>
#  include <cinttypes>
#  include <cstdlib>
#  include <climits>
#  include <fstream>
#  include <sstream>
#  include <string>

namespace {

std::string ReadFirstLine(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    return line;
}

class LinuxReader : public MemoryReader {
public:
    const char* Name() const override { return "linux (process_vm_readv)"; }

    bool Attach(const std::string& processName) override {
        // "shooter.exe" in the config -> the Linux binary is called "shooter".
        std::string name = processName;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0)
            name = name.substr(0, name.size() - 4);

        pid_ = 0;
        DIR* dir = opendir("/proc");
        if (!dir) return false;
        while (dirent* d = readdir(dir)) {
            char* end = nullptr;
            long pid = std::strtol(d->d_name, &end, 10);
            if (*end != 0 || pid <= 0 || pid == getpid()) continue;
            if (ReadFirstLine("/proc/" + std::string(d->d_name) + "/comm") == name.substr(0, 15)) {
                pid_ = static_cast<uint32_t>(pid);
                break;
            }
        }
        closedir(dir);
        if (!pid_) {
            std::fprintf(stderr, "[linux] process '%s' not found\n", name.c_str());
            return false;
        }

        // Main module = all mappings of the executable file.
        char exe[PATH_MAX] = {};
        std::string exeLink = "/proc/" + std::to_string(pid_) + "/exe";
        ssize_t n = readlink(exeLink.c_str(), exe, sizeof(exe) - 1);
        if (n <= 0) return false;
        exe[n] = 0;

        std::ifstream maps("/proc/" + std::to_string(pid_) + "/maps");
        uint64_t lo = UINT64_MAX, hi = 0;
        for (std::string line; std::getline(maps, line);) {
            if (line.size() < std::string(exe).size() ||
                line.compare(line.size() - std::string(exe).size(), std::string::npos, exe) != 0)
                continue;
            uint64_t a = 0, b = 0;
            if (std::sscanf(line.c_str(), "%" SCNx64 "-%" SCNx64, &a, &b) == 2) {
                lo = std::min(lo, a);
                hi = std::max(hi, b);
            }
        }
        if (hi == 0) return false;
        moduleBase_ = lo;
        moduleSize_ = hi - lo;
        return true;
    }

    bool Read(uint64_t address, void* buffer, size_t size) override {
        iovec local{buffer, size};
        iovec remote{reinterpret_cast<void*>(address), size};
        return process_vm_readv(static_cast<pid_t>(pid_), &local, 1, &remote, 1, 0) ==
               static_cast<ssize_t>(size);
    }
};

}  // namespace

std::unique_ptr<MemoryReader> CreateLinuxReader() { return std::make_unique<LinuxReader>(); }

#else

std::unique_ptr<MemoryReader> CreateLinuxReader() { return nullptr; }

#endif
