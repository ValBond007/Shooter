// =============================================================================
//  WinAPI backend: ReadProcessMemory on the SAME PC.
//
//  Only for testing the aimbot without DMA hardware (game and aimbot on one
//  machine). Logic-wise it is identical to the DMA backend: find the process,
//  find its module base, read bytes at virtual addresses.
// =============================================================================
#include "memory.h"

#include <cstdio>

#if defined(_WIN32)
#  undef UNICODE  // use the char (ANSI) versions of the Toolhelp structs
#  undef _UNICODE
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <tlhelp32.h>

namespace {

class WinApiReader : public MemoryReader {
public:
    ~WinApiReader() override {
        if (process_) CloseHandle(process_);
    }

    const char* Name() const override { return "winapi (ReadProcessMemory)"; }

    bool Attach(const std::string& processName) override {
        if (process_) { CloseHandle(process_); process_ = nullptr; }

        // 1) find the process id by name
        DWORD pid = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;
        PROCESSENTRY32 pe{};
        pe.dwSize = sizeof(pe);
        for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe)) {
            if (_stricmp(pe.szExeFile, processName.c_str()) == 0) { pid = pe.th32ProcessID; break; }
        }
        CloseHandle(snap);
        if (!pid) {
            std::fprintf(stderr, "[winapi] process '%s' not found\n", processName.c_str());
            return false;
        }

        // 2) find the main module (base address + size)
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) {
            std::fprintf(stderr, "[winapi] module snapshot failed (error %lu)\n", GetLastError());
            return false;
        }
        MODULEENTRY32 me{};
        me.dwSize = sizeof(me);
        bool found = false;
        for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
            if (_stricmp(me.szModule, processName.c_str()) == 0) {
                moduleBase_ = reinterpret_cast<uint64_t>(me.modBaseAddr);
                moduleSize_ = me.modBaseSize;
                found = true;
                break;
            }
        }
        CloseHandle(snap);
        if (!found) {
            std::fprintf(stderr, "[winapi] main module of '%s' not found\n", processName.c_str());
            return false;
        }

        // 3) open a handle that allows reading memory
        process_ = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process_) {
            std::fprintf(stderr, "[winapi] OpenProcess failed (error %lu)\n", GetLastError());
            return false;
        }
        pid_ = pid;
        return true;
    }

    bool Read(uint64_t address, void* buffer, size_t size) override {
        SIZE_T bytesRead = 0;
        return ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(address), buffer, size, &bytesRead) &&
               bytesRead == size;
    }

private:
    HANDLE process_ = nullptr;
};

}  // namespace

std::unique_ptr<MemoryReader> CreateWinApiReader() { return std::make_unique<WinApiReader>(); }

#else

std::unique_ptr<MemoryReader> CreateWinApiReader() { return nullptr; }

#endif
