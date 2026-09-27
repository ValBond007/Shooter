// =============================================================================
//  game_memory.h  -  THE memory layout of Arena Shooter
// =============================================================================
//
//  This header is shared by the game (shooter.exe) and the external reader
//  (aimbot.exe). Everything an external tool needs to know about the game is
//  stored in ONE global variable of type `GameMemory` inside the game:
//
//      GameMemory g_game;        // lives in the .data section of shooter.exe
//
//  Because it is a global with an initializer, it sits at a FIXED offset
//  (RVA) from the module base of shooter.exe. To find it from outside you can:
//
//    1. Use the RVA the game prints in debug mode (F1) / in shooter_offsets.txt
//         address = moduleBase("shooter.exe") + RVA
//    2. Scan the module for the 16 byte magic string "ARENA_SHOOTER!!\0" and
//       verify the candidate with `selfAddress` (the game writes the real
//       address of g_game into that field at startup).
//
//  All types have a fixed size and every offset is checked with
//  static_assert below, so the layout is identical for MSVC, MinGW and GCC
//  (64-bit). Offsets are written next to each field as comments.
//
//  Coordinate system
//  -----------------
//    World:  units = pixels at zoom 1.0, origin top-left of the arena,
//            +x = right, +y = down.
//    Screen: pixels inside the game window (client area), origin top-left.
//    World -> screen (same as raylib's GetWorldToScreen2D, rotation is 0):
//
//        screen = (world - camTarget) * camZoom + camOffset
//
//    Angles are in radians, 0 = pointing right (+x), PI/2 = pointing down.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace gm {

// 15 characters + terminating zero = 16 bytes.
#define GM_MAGIC_STRING "ARENA_SHOOTER!!"
constexpr char     kMagic[16]     = GM_MAGIC_STRING;
constexpr uint32_t kLayoutVersion = 4;

constexpr int kMaxEntities  = 32;  // slot 0 = local player, 1..31 = bots
constexpr int kMaxObstacles = 32;
constexpr int kMaxPickups   = 16;
constexpr int kMaxGrenades  = 16;
constexpr int kMaxBarrels   = 8;
constexpr int kNameLength   = 16;

enum Team : uint32_t {
    TEAM_PLAYER = 0,
    TEAM_BOTS   = 1,
};

// What kind of entity (Entity::kind).
enum EntityKind : uint32_t {
    KIND_PLAYER  = 0,
    KIND_SOLDIER = 1,  // normal bot
    KIND_RUNNER  = 2,  // fast, small, weak
    KIND_HEAVY   = 3,  // slow, big, lots of health, shotgun
};

// Weapons (Entity::weapon). Every weapon has its own bullet speed.
enum WeaponId : uint32_t {
    WEAPON_RIFLE   = 0,
    WEAPON_SHOTGUN = 1,
    WEAPON_SNIPER  = 2,
};
constexpr int kWeaponCount = 3;

enum Difficulty : uint32_t {
    DIFFICULTY_EASY   = 0,
    DIFFICULTY_NORMAL = 1,
    DIFFICULTY_HARD   = 2,
};

enum GameMode : uint32_t {
    MODE_DEATHMATCH      = 0,  // everyone against you, first to the kill limit
    MODE_TEAM_DEATHMATCH = 1,  // you + allied bots (team 0) vs enemy bots (team 1)
    MODE_SURVIVAL        = 2,  // waves of enemies, 3 lives
    MODE_TRAINING        = 3,  // aim training: one moving target at a time, stats
};

enum MatchState : uint32_t {
    MATCH_MENU    = 0,  // main menu, nothing is simulated
    MATCH_PLAYING = 1,
    MATCH_ENDED   = 2,  // result screen
};

enum PickupType : uint32_t {
    PICKUP_HEALTH  = 0,  // +40% health
    PICKUP_SPEED   = 1,  // 40% faster for 8 s
    PICKUP_DAMAGE  = 2,  // double damage for 8 s
    PICKUP_SHIELD  = 3,  // +50 shield (absorbs damage first)
    PICKUP_GRENADE = 4,  // +1 grenade
};

// Bits of GameMemory::buttons (mouse buttons currently held in the game).
enum Buttons : uint32_t {
    BUTTON_LEFT   = 1u << 0,
    BUTTON_RIGHT  = 1u << 1,
    BUTTON_MIDDLE = 1u << 2,
};

struct Vec2f {
    float x;
    float y;
};

// One player or bot. Size: 0x60 (96) bytes.
struct Entity {
    uint32_t id;             // 0x00  unique id, changes every (re)spawn
    uint32_t active;         // 0x04  1 = slot in use, 0 = empty slot
    uint32_t alive;          // 0x08  1 = alive, 0 = dead (waiting to respawn)
    uint32_t team;           // 0x0C  gm::Team
    int32_t  health;         // 0x10
    int32_t  maxHealth;      // 0x14
    Vec2f    pos;            // 0x18  world position (center)
    Vec2f    vel;            // 0x20  velocity in world units / second
    float    aimAngle;       // 0x28  direction the gun points (radians)
    float    radius;         // 0x2C  collision radius in world units
    uint32_t visible;        // 0x30  1 = local player has line of sight to it
    int32_t  kills;          // 0x34
    int32_t  deaths;         // 0x38
    float    respawnTimer;   // 0x3C  seconds until respawn (when dead)
    char     name[kNameLength]; // 0x40  zero terminated
    uint32_t weapon;         // 0x50  gm::WeaponId currently held
    int32_t  ammo;           // 0x54  rounds left in the magazine
    float    reloadTimer;    // 0x58  > 0 while reloading (seconds left)
    uint32_t kind;           // 0x5C  gm::EntityKind
};

// Axis aligned wall / crate. Size: 0x10 bytes.
struct Obstacle {
    float x;                 // 0x00  top-left corner (world)
    float y;                 // 0x04
    float w;                 // 0x08  size
    float h;                 // 0x0C
};

// Health pack etc. lying in the arena. Size: 0x10 bytes.
struct Pickup {
    Vec2f    pos;            // 0x00  world position (center)
    uint32_t type;           // 0x08  gm::PickupType
    uint32_t available;      // 0x0C  1 = can be picked up, 0 = respawning / unused
};

// Current match. Size: 0x20 bytes.
struct MatchInfo {
    uint32_t mode;           // 0x00  gm::GameMode
    uint32_t state;          // 0x04  gm::MatchState
    float    timeLeft;       // 0x08  seconds (0 = no time limit)
    uint32_t map;            // 0x0C  map index
    int32_t  teamScore[2];   // 0x10  kills per team (team 0 = your team)
    uint32_t wave;           // 0x18  survival: current wave
    int32_t  livesLeft;      // 0x1C  survival: lives left
};

// A thrown grenade. Size: 0x20 bytes.
struct Grenade {
    Vec2f    pos;            // 0x00  world position
    Vec2f    vel;            // 0x08  units / second
    float    fuse;           // 0x10  seconds until it explodes
    uint32_t active;         // 0x14  1 = flying / lying on the ground
    uint32_t team;           // 0x18  team of the thrower
    int32_t  owner;          // 0x1C  entity index of the thrower
};

// Explosive barrel. Size: 0x10 bytes.
struct Barrel {
    Vec2f    pos;            // 0x00  world position (center, radius 20)
    int32_t  health;         // 0x08
    uint32_t alive;          // 0x0C  0 = exploded, respawning / unused
};

// Temporary effects per entity (same index as entities[]). Size: 0x10 bytes.
struct EntityBuffs {
    float    speedTime;      // 0x00  seconds of speed boost left
    float    damageTime;     // 0x04  seconds of double damage left
    int32_t  shield;         // 0x08  shield points (absorb damage before health)
    int32_t  grenades;       // 0x0C  grenades carried
};

// The global game state. Size: 0x14A0 bytes.
struct GameMemory {
    // ---- header ----------------------------------------------------------
    char     magic[16];          // 0x000  "ARENA_SHOOTER!!"
    uint32_t layoutVersion;      // 0x010  kLayoutVersion
    uint32_t structSize;         // 0x014  sizeof(GameMemory)
    uint64_t selfAddress;        // 0x018  address of this struct in the game
    uint32_t frameCount;         // 0x020  +1 every rendered frame
    float    gameTime;           // 0x024  seconds since start
    uint32_t localPlayerIndex;   // 0x028  index into entities[] (always 0)
    uint32_t entityCount;        // 0x02C  used slots in entities[]

    // ---- camera (raylib Camera2D) -----------------------------------------
    Vec2f    camTarget;          // 0x030  world point that is drawn at camOffset
    Vec2f    camOffset;          // 0x038  screen point (= window center)
    float    camZoom;            // 0x040  1.0 = 1 world unit per pixel
    float    camRotation;        // 0x044  always 0
    int32_t  screenWidth;        // 0x048  window client size
    int32_t  screenHeight;       // 0x04C

    // ---- input ------------------------------------------------------------
    Vec2f    mouseScreen;        // 0x050  cursor, window client coordinates
    Vec2f    mouseWorld;         // 0x058  cursor in world coordinates
    uint32_t buttons;            // 0x060  gm::Buttons bit mask
    uint32_t paused;             // 0x064  1 = game paused

    // ---- world ------------------------------------------------------------
    Vec2f    arenaSize;          // 0x068  arena is [0,0] .. arenaSize
    float    bulletSpeed;        // 0x070  bullet speed of the local player's CURRENT weapon (units/s)
    uint32_t obstacleCount;      // 0x074
    uint32_t difficulty;         // 0x078  gm::Difficulty
    uint32_t fogOfWar;           // 0x07C  1 = hidden enemies are not drawn

    Entity   entities[kMaxEntities];    // 0x080  32 * 0x60 = 0xC00
    Obstacle obstacles[kMaxObstacles];  // 0xC80  32 * 0x10 = 0x200
    Pickup   pickups[kMaxPickups];      // 0xE80  16 * 0x10 = 0x100
    MatchInfo match;                    // 0xF80
    Grenade  grenades[kMaxGrenades];    // 0xFA0  16 * 0x20 = 0x200
    Barrel   barrels[kMaxBarrels];      // 0x11A0  8 * 0x10 = 0x80
    uint8_t  _reserved2[0x80];          // 0x1220
    EntityBuffs buffs[kMaxEntities];    // 0x12A0  32 * 0x10 = 0x200
};                                      // 0x14A0  end

// ---- layout checks (compile error if anything moves) ------------------------
static_assert(sizeof(Vec2f) == 0x08, "Vec2f size");
static_assert(sizeof(Entity) == 0x60, "Entity size");
static_assert(sizeof(Obstacle) == 0x10, "Obstacle size");
static_assert(sizeof(Pickup) == 0x10, "Pickup size");
static_assert(sizeof(MatchInfo) == 0x20, "MatchInfo size");
static_assert(sizeof(Grenade) == 0x20, "Grenade size");
static_assert(sizeof(Barrel) == 0x10, "Barrel size");
static_assert(sizeof(EntityBuffs) == 0x10, "EntityBuffs size");
static_assert(sizeof(GameMemory) == 0x14A0, "GameMemory size");

static_assert(offsetof(Entity, id) == 0x00, "");
static_assert(offsetof(Entity, active) == 0x04, "");
static_assert(offsetof(Entity, alive) == 0x08, "");
static_assert(offsetof(Entity, team) == 0x0C, "");
static_assert(offsetof(Entity, health) == 0x10, "");
static_assert(offsetof(Entity, maxHealth) == 0x14, "");
static_assert(offsetof(Entity, pos) == 0x18, "");
static_assert(offsetof(Entity, vel) == 0x20, "");
static_assert(offsetof(Entity, aimAngle) == 0x28, "");
static_assert(offsetof(Entity, radius) == 0x2C, "");
static_assert(offsetof(Entity, visible) == 0x30, "");
static_assert(offsetof(Entity, kills) == 0x34, "");
static_assert(offsetof(Entity, deaths) == 0x38, "");
static_assert(offsetof(Entity, respawnTimer) == 0x3C, "");
static_assert(offsetof(Entity, name) == 0x40, "");
static_assert(offsetof(Entity, weapon) == 0x50, "");
static_assert(offsetof(Entity, ammo) == 0x54, "");
static_assert(offsetof(Entity, reloadTimer) == 0x58, "");
static_assert(offsetof(Entity, kind) == 0x5C, "");

static_assert(offsetof(GameMemory, magic) == 0x000, "");
static_assert(offsetof(GameMemory, layoutVersion) == 0x010, "");
static_assert(offsetof(GameMemory, structSize) == 0x014, "");
static_assert(offsetof(GameMemory, selfAddress) == 0x018, "");
static_assert(offsetof(GameMemory, frameCount) == 0x020, "");
static_assert(offsetof(GameMemory, gameTime) == 0x024, "");
static_assert(offsetof(GameMemory, localPlayerIndex) == 0x028, "");
static_assert(offsetof(GameMemory, entityCount) == 0x02C, "");
static_assert(offsetof(GameMemory, camTarget) == 0x030, "");
static_assert(offsetof(GameMemory, camOffset) == 0x038, "");
static_assert(offsetof(GameMemory, camZoom) == 0x040, "");
static_assert(offsetof(GameMemory, camRotation) == 0x044, "");
static_assert(offsetof(GameMemory, screenWidth) == 0x048, "");
static_assert(offsetof(GameMemory, screenHeight) == 0x04C, "");
static_assert(offsetof(GameMemory, mouseScreen) == 0x050, "");
static_assert(offsetof(GameMemory, mouseWorld) == 0x058, "");
static_assert(offsetof(GameMemory, buttons) == 0x060, "");
static_assert(offsetof(GameMemory, paused) == 0x064, "");
static_assert(offsetof(GameMemory, arenaSize) == 0x068, "");
static_assert(offsetof(GameMemory, bulletSpeed) == 0x070, "");
static_assert(offsetof(GameMemory, obstacleCount) == 0x074, "");
static_assert(offsetof(GameMemory, difficulty) == 0x078, "");
static_assert(offsetof(GameMemory, fogOfWar) == 0x07C, "");
static_assert(offsetof(GameMemory, entities) == 0x080, "");
static_assert(offsetof(GameMemory, obstacles) == 0xC80, "");
static_assert(offsetof(GameMemory, pickups) == 0xE80, "");
static_assert(offsetof(GameMemory, match) == 0xF80, "");
static_assert(offsetof(GameMemory, grenades) == 0xFA0, "");
static_assert(offsetof(GameMemory, barrels) == 0x11A0, "");
static_assert(offsetof(GameMemory, buffs) == 0x12A0, "");

// ---- helpers ------------------------------------------------------------------

// World -> screen, exactly like the game renders it.
inline Vec2f WorldToScreen(const GameMemory& g, Vec2f world) {
    return Vec2f{(world.x - g.camTarget.x) * g.camZoom + g.camOffset.x,
                 (world.y - g.camTarget.y) * g.camZoom + g.camOffset.y};
}

// Screen -> world (inverse of the above).
inline Vec2f ScreenToWorld(const GameMemory& g, Vec2f screen) {
    return Vec2f{(screen.x - g.camOffset.x) / g.camZoom + g.camTarget.x,
                 (screen.y - g.camOffset.y) / g.camZoom + g.camTarget.y};
}

}  // namespace gm
