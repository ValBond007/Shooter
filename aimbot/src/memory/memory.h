// =============================================================================
//  MemoryReader - "how do we get bytes out of the game process?"
//
//  Implementations:
//    dma     : MemProcFS / PCILeech + FPGA card (the real project setup,
//              reads the game PC's RAM from a second PC)          memory_dma.cpp
//    winapi  : ReadProcessMemory on the same PC (for testing)   memory_winapi.cpp
//    linux   : process_vm_readv on the same PC (dev/testing)    memory_linux.cpp
//
//  The rest of the program only uses this interface, so switching from local
//  testing to DMA is a single line in config.ini.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class MemoryReader {
public:
    virtual ~MemoryReader() = default;

    virtual const char* Name() const = 0;

    // Find the process by executable name (e.g. "shooter.exe") and its main
    // module. Returns false (and prints why) on failure.
    virtual bool Attach(const std::string& processName) = 0;

    // Read `size` bytes at virtual address `address` of the attached process.
    virtual bool Read(uint64_t address, void* buffer, size_t size) = 0;

    template <class T>
    bool Read(uint64_t address, T& out) { return Read(address, &out, sizeof(T)); }

    uint32_t Pid() const { return pid_; }
    uint64_t ModuleBase() const { return moduleBase_; }
    uint64_t ModuleSize() const { return moduleSize_; }

protected:
    uint32_t pid_        = 0;
    uint64_t moduleBase_ = 0;
    uint64_t moduleSize_ = 0;
};

// Reads the PE header at `moduleBase` and returns SizeOfImage (0 on error).
// (Windows executables only: MZ -> e_lfanew -> PE\0\0 -> OptionalHeader.)
uint32_t ReadPeSizeOfImage(MemoryReader& mem, uint64_t moduleBase);

// Factories. They return nullptr if the backend is not available on this OS.
std::unique_ptr<MemoryReader> CreateDmaReader(const std::string& vmmArguments);
std::unique_ptr<MemoryReader> CreateWinApiReader();
std::unique_ptr<MemoryReader> CreateLinuxReader();

// Creates the reader selected by name ("dma", "winapi", "linux").
std::unique_ptr<MemoryReader> CreateMemoryReader(const std::string& type,
                                                 const std::string& vmmArguments);
