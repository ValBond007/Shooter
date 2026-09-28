// NOTE: this file must not include raylib.h (windows.h and raylib clash).
#include "memory_export.h"

#include <cinttypes>
#include <cstddef>
#include <cstdio>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#endif

// -----------------------------------------------------------------------------
//  The global game state.
//
//  It has an initializer, so the compiler puts it into the .data section of
//  the executable -> it is at a fixed RVA (offset from the module base) and
//  the magic string is physically inside the module's memory.
// -----------------------------------------------------------------------------
gm::GameMemory g_game = {
    GM_MAGIC_STRING,              // magic
    gm::kLayoutVersion,           // layoutVersion
    sizeof(gm::GameMemory),       // structSize
};

uint64_t GetModuleBaseAddress() {
#if defined(_WIN32)
    return reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&g_game), &info) && info.dli_fbase)
        return reinterpret_cast<uint64_t>(info.dli_fbase);
    return 0;
#endif
}

std::string GetExecutableName() {
#if defined(_WIN32)
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
#else
    char path[4096] = {};
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) path[n] = 0;
#endif
    std::string s(path);
    size_t slash = s.find_last_of("/\\");
    return slash == std::string::npos ? s : s.substr(slash + 1);
}

uint32_t GetProcessIdSelf() {
#if defined(_WIN32)
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

std::string DescribeMemoryLayout() {
    const uint64_t base = GetModuleBaseAddress();
    const uint64_t addr = reinterpret_cast<uint64_t>(&g_game);
    const uint64_t ents = addr + offsetof(gm::GameMemory, entities);

    std::string out;
    char line[256];
    auto add = [&](const char* fmt, auto... args) {
        if constexpr (sizeof...(args) == 0) {
            out += fmt;
        } else {
            std::snprintf(line, sizeof(line), fmt, args...);
            out += line;
        }
        out += '\n';
    };

    add("================ Arena Shooter - memory layout v%u ================", gm::kLayoutVersion);
    add("process            : %s (pid %u)", GetExecutableName().c_str(), GetProcessIdSelf());
    add("module base        : 0x%016" PRIX64, base);
    add("g_game address     : 0x%016" PRIX64, addr);
    add("g_game RVA         : 0x%" PRIX64 "   (address = module base + RVA)", addr - base);
    add("magic              : \"%s\" (16 bytes, at offset 0x000)", GM_MAGIC_STRING);
    add("sizeof(GameMemory) : 0x%zX (%zu bytes)", sizeof(gm::GameMemory), sizeof(gm::GameMemory));
    add("sizeof(Entity)     : 0x%zX (%zu bytes)", sizeof(gm::Entity), sizeof(gm::Entity));
    add("");
    add("GameMemory fields (offset from g_game):");
#define GM_FIELD(name, type) add("  +0x%03zX  %-8s %s", offsetof(gm::GameMemory, name), type, #name)
    GM_FIELD(magic, "char[16]");
    GM_FIELD(layoutVersion, "u32");
    GM_FIELD(structSize, "u32");
    GM_FIELD(selfAddress, "u64");
    GM_FIELD(frameCount, "u32");
    GM_FIELD(gameTime, "f32");
    GM_FIELD(localPlayerIndex, "u32");
    GM_FIELD(entityCount, "u32");
    GM_FIELD(camTarget, "f32[2]");
    GM_FIELD(camOffset, "f32[2]");
    GM_FIELD(camZoom, "f32");
    GM_FIELD(camRotation, "f32");
    GM_FIELD(screenWidth, "i32");
    GM_FIELD(screenHeight, "i32");
    GM_FIELD(mouseScreen, "f32[2]");
    GM_FIELD(mouseWorld, "f32[2]");
    GM_FIELD(buttons, "u32");
    GM_FIELD(paused, "u32");
    GM_FIELD(arenaSize, "f32[2]");
    GM_FIELD(bulletSpeed, "f32");
    GM_FIELD(obstacleCount, "u32");
    GM_FIELD(difficulty, "u32");
    GM_FIELD(fogOfWar, "u32");
    GM_FIELD(entities, "Entity[32]");
    GM_FIELD(obstacles, "Obst.[32]");
    GM_FIELD(pickups, "Pick.[16]");
    GM_FIELD(match, "MatchInfo");
    GM_FIELD(grenades, "Gren.[16]");
    GM_FIELD(barrels, "Barr.[8]");
    GM_FIELD(buffs, "Buffs[32]");
#undef GM_FIELD
    add("");
    add("Entity fields (offset from entities[i]):");
#define GM_EFIELD(name, type) add("  +0x%02zX  %-8s %s", offsetof(gm::Entity, name), type, #name)
    GM_EFIELD(id, "u32");
    GM_EFIELD(active, "u32");
    GM_EFIELD(alive, "u32");
    GM_EFIELD(team, "u32");
    GM_EFIELD(health, "i32");
    GM_EFIELD(maxHealth, "i32");
    GM_EFIELD(pos, "f32[2]");
    GM_EFIELD(vel, "f32[2]");
    GM_EFIELD(aimAngle, "f32");
    GM_EFIELD(radius, "f32");
    GM_EFIELD(visible, "u32");
    GM_EFIELD(kills, "i32");
    GM_EFIELD(deaths, "i32");
    GM_EFIELD(respawnTimer, "f32");
    GM_EFIELD(name, "char[16]");
    GM_EFIELD(weapon, "u32");
    GM_EFIELD(ammo, "i32");
    GM_EFIELD(reloadTimer, "f32");
    GM_EFIELD(kind, "u32");
#undef GM_EFIELD
    add("");
    add("entities[i] address = 0x%016" PRIX64 " + i * 0x%zX", ents, sizeof(gm::Entity));
    add("local player        = entities[0] at 0x%016" PRIX64, ents);
    add("pickups[i] address  = 0x%016" PRIX64 " + i * 0x%zX  (pos f32[2], type u32, available u32)",
        addr + offsetof(gm::GameMemory, pickups), sizeof(gm::Pickup));
    add("match               = g_game + 0x%zX  (mode u32, state u32, timeLeft f32, map u32,",
        offsetof(gm::GameMemory, match));
    add("                        teamScore i32[2], wave u32, livesLeft i32)");
    add("grenades[i]         = g_game + 0x%zX + i * 0x%zX  (pos, vel, fuse f32, active u32, team u32, owner i32)",
        offsetof(gm::GameMemory, grenades), sizeof(gm::Grenade));
    add("barrels[i]          = g_game + 0x%zX + i * 0x%zX  (pos, health i32, alive u32)",
        offsetof(gm::GameMemory, barrels), sizeof(gm::Barrel));
    add("buffs[i]            = g_game + 0x%zX + i * 0x%zX  (speedTime f32, damageTime f32, shield i32, grenades i32)",
        offsetof(gm::GameMemory, buffs), sizeof(gm::EntityBuffs));
    add("world -> screen     : screen = (world - camTarget) * camZoom + camOffset");
    add("===================================================================");
    return out;
}

bool WriteOffsetsFile(const char* path) {
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    std::string text = DescribeMemoryLayout();
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    return true;
}
