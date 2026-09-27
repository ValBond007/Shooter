// =============================================================================
//  DMA backend: MemProcFS (vmm.dll) + LeechCore + FPGA DMA card.
//
//  The DMA card sits in the GAME PC and reads its physical RAM over PCIe.
//  MemProcFS (running on this, the second PC) turns physical memory into
//  processes / modules / virtual addresses for us, by walking the Windows
//  kernel structures and page tables of the game PC.
//
//  We load vmm.dll at runtime (LoadLibrary + GetProcAddress), so the project
//  builds without the MemProcFS SDK. At runtime these files must be next to
//  aimbot.exe (all from the MemProcFS release / FTDI):
//      vmm.dll, leechcore.dll, FTD3XX.dll  (+ optional: info.db, dbghelp.dll, symsrv.dll)
//
//  Functions used (see vmmdll.h in the MemProcFS repository):
//      VMMDLL_Initialize(argc, argv)                      -> VMM_HANDLE
//      VMMDLL_PidGetFromName(h, "shooter.exe", &pid)
//      VMMDLL_ProcessGetModuleBaseU(h, pid, "shooter.exe") -> module base
//      VMMDLL_MemReadEx(h, pid, address, buf, size, &read, flags)
//      VMMDLL_Close(h)
// =============================================================================
#include "memory.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>

namespace {

typedef struct tdVMM_HANDLE* VMM_HANDLE;

using PFN_VMMDLL_Initialize           = VMM_HANDLE (*)(DWORD argc, LPCSTR argv[]);
using PFN_VMMDLL_Close                = VOID (*)(VMM_HANDLE hVMM);
using PFN_VMMDLL_PidGetFromName       = BOOL (*)(VMM_HANDLE hVMM, LPCSTR szProcName, PDWORD pdwPID);
using PFN_VMMDLL_ProcessGetModuleBaseU = ULONG64 (*)(VMM_HANDLE hVMM, DWORD dwPID, LPCSTR uszModuleName);
using PFN_VMMDLL_MemReadEx            = BOOL (*)(VMM_HANDLE hVMM, DWORD dwPID, ULONG64 qwA, PBYTE pb,
                                                 DWORD cb, PDWORD pcbReadOpt, ULONG64 flags);

// GetProcAddress returns a generic function pointer; cast it to the real type.
template <class T>
T GetFunction(HMODULE dll, const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(dll, name)));
}

constexpr ULONG64 VMMDLL_FLAG_NOCACHE = 0x0001;  // always read fresh memory (no cache)

class DmaReader : public MemoryReader {
public:
    explicit DmaReader(std::string args) : args_(std::move(args)) {}

    ~DmaReader() override {
        if (vmm_ && fnClose_) fnClose_(vmm_);
        if (dll_) FreeLibrary(dll_);
    }

    const char* Name() const override { return "dma (MemProcFS)"; }

    bool Attach(const std::string& processName) override {
        if (!vmm_ && !Initialize()) return false;

        DWORD pid = 0;
        if (!fnPidGetFromName_(vmm_, processName.c_str(), &pid) || pid == 0) {
            std::fprintf(stderr, "[dma] process '%s' not found on the target PC\n", processName.c_str());
            return false;
        }
        pid_ = pid;
        moduleBase_ = fnGetModuleBase_(vmm_, pid, processName.c_str());
        if (!moduleBase_) {
            std::fprintf(stderr, "[dma] could not get module base of '%s'\n", processName.c_str());
            return false;
        }
        moduleSize_ = ReadPeSizeOfImage(*this, moduleBase_);
        return true;
    }

    bool Read(uint64_t address, void* buffer, size_t size) override {
        DWORD bytesRead = 0;
        BOOL ok = fnMemReadEx_(vmm_, static_cast<DWORD>(pid_), address, static_cast<PBYTE>(buffer),
                               static_cast<DWORD>(size), &bytesRead, VMMDLL_FLAG_NOCACHE);
        return ok && bytesRead == size;
    }

private:
    bool Initialize() {
        dll_ = LoadLibraryA("vmm.dll");
        if (!dll_) {
            std::fprintf(stderr,
                         "[dma] could not load vmm.dll (error %lu).\n"
                         "      Put vmm.dll, leechcore.dll and FTD3XX.dll next to aimbot.exe.\n",
                         GetLastError());
            return false;
        }
        fnInitialize_     = GetFunction<PFN_VMMDLL_Initialize>(dll_, "VMMDLL_Initialize");
        fnClose_          = GetFunction<PFN_VMMDLL_Close>(dll_, "VMMDLL_Close");
        fnPidGetFromName_ = GetFunction<PFN_VMMDLL_PidGetFromName>(dll_, "VMMDLL_PidGetFromName");
        fnGetModuleBase_  = GetFunction<PFN_VMMDLL_ProcessGetModuleBaseU>(dll_, "VMMDLL_ProcessGetModuleBaseU");
        fnMemReadEx_      = GetFunction<PFN_VMMDLL_MemReadEx>(dll_, "VMMDLL_MemReadEx");
        if (!fnInitialize_ || !fnClose_ || !fnPidGetFromName_ || !fnGetModuleBase_ || !fnMemReadEx_) {
            std::fprintf(stderr, "[dma] vmm.dll is missing functions - use MemProcFS 5.x\n");
            return false;
        }

        // Build argv like on the command line: "" -device fpga ...
        std::vector<std::string> parts{""};
        std::istringstream ss(args_);
        for (std::string p; ss >> p;) parts.push_back(p);
        std::vector<LPCSTR> argv;
        for (const auto& p : parts) argv.push_back(p.c_str());

        std::printf("[dma] VMMDLL_Initialize(%s) ...\n", args_.c_str());
        vmm_ = fnInitialize_(static_cast<DWORD>(argv.size()), argv.data());
        if (!vmm_) {
            std::fprintf(stderr,
                         "[dma] VMMDLL_Initialize failed. Is the DMA card connected and the\n"
                         "      game PC turned on? Try running MemProcFS.exe %s by hand.\n",
                         args_.c_str());
            return false;
        }
        std::printf("[dma] connected to the target PC\n");
        return true;
    }

    std::string args_;
    HMODULE     dll_ = nullptr;
    VMM_HANDLE  vmm_ = nullptr;

    PFN_VMMDLL_Initialize            fnInitialize_     = nullptr;
    PFN_VMMDLL_Close                 fnClose_          = nullptr;
    PFN_VMMDLL_PidGetFromName        fnPidGetFromName_ = nullptr;
    PFN_VMMDLL_ProcessGetModuleBaseU fnGetModuleBase_  = nullptr;
    PFN_VMMDLL_MemReadEx             fnMemReadEx_      = nullptr;
};

}  // namespace

std::unique_ptr<MemoryReader> CreateDmaReader(const std::string& vmmArguments) {
    return std::make_unique<DmaReader>(vmmArguments);
}

#else  // !_WIN32

std::unique_ptr<MemoryReader> CreateDmaReader(const std::string&) { return nullptr; }

#endif
