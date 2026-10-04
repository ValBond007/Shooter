#include "memory.h"

#include <cstdio>
#include <cstring>

uint32_t ReadPeSizeOfImage(MemoryReader& mem, uint64_t moduleBase) {
    // IMAGE_DOS_HEADER: "MZ" at 0x00, e_lfanew (offset of the NT headers) at 0x3C
    uint16_t mz = 0;
    int32_t  lfanew = 0;
    if (!mem.Read(moduleBase, mz) || mz != 0x5A4D) return 0;
    if (!mem.Read(moduleBase + 0x3C, lfanew) || lfanew <= 0 || lfanew > 0x1000) return 0;

    // IMAGE_NT_HEADERS64: "PE\0\0", FileHeader (0x14 bytes), OptionalHeader.
    // OptionalHeader.SizeOfImage is at +0x38 inside the optional header,
    // i.e. NT headers + 4 + 0x14 + 0x38 = NT headers + 0x50.
    uint32_t signature = 0, sizeOfImage = 0;
    if (!mem.Read(moduleBase + lfanew, signature) || signature != 0x00004550) return 0;
    if (!mem.Read(moduleBase + lfanew + 0x50, sizeOfImage)) return 0;
    return sizeOfImage;
}

std::unique_ptr<MemoryReader> CreateMemoryReader(const std::string& type,
                                                 const std::string& vmmArguments) {
    std::unique_ptr<MemoryReader> r;
    if (type == "dma") r = CreateDmaReader(vmmArguments);
    else if (type == "winapi") r = CreateWinApiReader();
    else if (type == "linux") r = CreateLinuxReader();
    else {
        std::fprintf(stderr, "[memory] unknown memory backend '%s' (use dma, winapi or linux)\n",
                     type.c_str());
        return nullptr;
    }
    if (!r) std::fprintf(stderr, "[memory] backend '%s' is not available on this OS\n", type.c_str());
    return r;
}
