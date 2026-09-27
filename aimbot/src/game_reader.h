// =============================================================================
//  GameReader - finds the game's global `g_game` struct and reads snapshots.
// =============================================================================
#pragma once

#include <cstdint>

#include "game_memory.h"
#include "memory/memory.h"

class GameReader {
public:
    // configuredRva: 0 = find g_game automatically by scanning for the magic
    //                string, otherwise use moduleBase + configuredRva.
    GameReader(MemoryReader& mem, uint64_t configuredRva) : mem_(mem), rva_(configuredRva) {}

    // Finds the address of g_game in the attached process.
    bool Locate();

    // Reads the whole GameMemory struct (one read of 0xF80 bytes) and checks
    // that it is still valid. Returns false if the read failed or the magic
    // is wrong (e.g. the game was closed/restarted -> call Locate() again).
    bool ReadSnapshot(gm::GameMemory& out);

    uint64_t Address() const { return address_; }
    bool     Located() const { return address_ != 0; }

private:
    bool Validate(uint64_t candidate);
    uint64_t ScanForMagic();

    MemoryReader& mem_;
    uint64_t      rva_     = 0;
    uint64_t      address_ = 0;
};
