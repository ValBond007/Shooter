#include "game_reader.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <vector>

bool GameReader::Validate(uint64_t candidate) {
    // Read just the header and check every field we know.
    struct Header {
        char     magic[16];
        uint32_t layoutVersion;
        uint32_t structSize;
        uint64_t selfAddress;
    } h{};
    static_assert(sizeof(Header) == offsetof(gm::GameMemory, frameCount), "header layout");

    if (!mem_.Read(candidate, h)) return false;
    if (std::memcmp(h.magic, gm::kMagic, sizeof(h.magic)) != 0) return false;
    if (h.layoutVersion != gm::kLayoutVersion) {
        std::fprintf(stderr, "[reader] layout version mismatch: game %u, reader %u (rebuild both)\n",
                     h.layoutVersion, gm::kLayoutVersion);
        return false;
    }
    if (h.structSize != sizeof(gm::GameMemory)) return false;
    // The game writes &g_game into selfAddress -> this proves we have the real
    // struct and not just another copy of the magic string somewhere.
    return h.selfAddress == candidate;
}

uint64_t GameReader::ScanForMagic() {
    const uint64_t base = mem_.ModuleBase();
    const uint64_t size = mem_.ModuleSize();
    if (!base || !size) {
        std::fprintf(stderr, "[reader] module size unknown - cannot scan\n");
        return 0;
    }
    std::printf("[reader] scanning module 0x%" PRIX64 " - 0x%" PRIX64 " (%" PRIu64 " KiB) for \"%s\"...\n",
                base, base + size, size / 1024, GM_MAGIC_STRING);

    // Read the module in 64 KiB chunks. Chunks overlap by 16 bytes so a magic
    // string on a chunk border is still found. Unreadable chunks are skipped.
    const size_t chunk = 64 * 1024;
    const size_t overlap = sizeof(gm::kMagic);
    std::vector<uint8_t> buf(chunk + overlap);

    for (uint64_t offset = 0; offset < size; offset += chunk) {
        size_t toRead = static_cast<size_t>(std::min<uint64_t>(chunk + overlap, size - offset));
        if (!mem_.Read(base + offset, buf.data(), toRead)) {
            // Maybe partially unreadable: fall back to 4 KiB pages.
            for (size_t page = 0; page < toRead; page += 4096) {
                size_t n = std::min<size_t>(4096, toRead - page);
                if (!mem_.Read(base + offset + page, buf.data() + page, n))
                    std::memset(buf.data() + page, 0, n);
            }
        }
        // The struct is 16-byte aligned in practice, but scan every 8 bytes to be safe.
        for (size_t i = 0; i + overlap <= toRead; i += 8) {
            if (std::memcmp(buf.data() + i, gm::kMagic, sizeof(gm::kMagic)) != 0) continue;
            uint64_t candidate = base + offset + i;
            if (Validate(candidate)) return candidate;
        }
    }
    return 0;
}

bool GameReader::Locate() {
    address_ = 0;
    uint64_t candidate = 0;
    if (rva_ != 0) {
        candidate = mem_.ModuleBase() + rva_;
        std::printf("[reader] using configured RVA 0x%" PRIX64 " -> 0x%" PRIX64 "\n", rva_, candidate);
        if (!Validate(candidate)) {
            std::fprintf(stderr, "[reader] no valid GameMemory at that address (wrong game_rva?)\n");
            return false;
        }
    } else {
        candidate = ScanForMagic();
        if (!candidate) {
            std::fprintf(stderr, "[reader] magic not found (is the game running and fully started?)\n");
            return false;
        }
    }
    address_ = candidate;
    std::printf("[reader] g_game found at 0x%" PRIX64 " (RVA 0x%" PRIX64 ")\n", address_,
                address_ - mem_.ModuleBase());
    return true;
}

bool GameReader::ReadSnapshot(gm::GameMemory& out) {
    if (!address_) return false;
    // ONE read for the whole game state: header, camera, input and all
    // entities. For DMA this is important - every read is a round trip over
    // PCIe, so few big reads are much faster than many small ones.
    if (!mem_.Read(address_, &out, sizeof(out))) return false;
    return std::memcmp(out.magic, gm::kMagic, sizeof(gm::kMagic)) == 0 &&
           out.selfAddress == address_;
}
