// The one global that external tools read, plus helpers that print where it
// lives (debug mode, console, shooter_offsets.txt).
#pragma once

#include <cstdint>
#include <string>

#include "game_memory.h"

// THE game state. Defined in memory_export.cpp.
extern gm::GameMemory g_game;

// Base address of the game's own executable module (shooter.exe).
uint64_t GetModuleBaseAddress();

// Name of the running executable (e.g. "shooter.exe").
std::string GetExecutableName();

uint32_t GetProcessIdSelf();

// Multi-line description of all addresses and offsets.
std::string DescribeMemoryLayout();

// Writes DescribeMemoryLayout() to a text file. Returns false on error.
bool WriteOffsetsFile(const char* path);
