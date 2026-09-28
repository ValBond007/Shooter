#include "game.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "maps.h"

namespace {

constexpr float kPi = 3.14159265358979f;

// ---- tuning -------------------------------------------------------------------
constexpr float kArenaW = 2400.0f;
constexpr float kArenaH = 1600.0f;

constexpr float kPlayerRadius    = 18.0f;
constexpr float kPlayerSpeed     = 290.0f;
constexpr float kPlayerAccel     = 3200.0f;
constexpr int   kPlayerMaxHealth = 100;
constexpr float kPlayerRespawn   = 3.0f;
constexpr float kSpawnProtection = 2.0f;

constexpr float kBotAccel     = 2200.0f;
constexpr float kBotRespawn   = 2.5f;
constexpr float kBotViewRange = 1100.0f;
constexpr float kBotTurnSpeed = 7.0f;  // radians / second
constexpr float kBotReload    = 2.0f;

constexpr float kBulletRadius = 3.0f;

constexpr int   kHealthPackAmount  = 40;
constexpr float kHealthPackRespawn = 20.0f;
constexpr float kPickupRadius      = 16.0f;

// ---- player dash ---------------------------------------------------------------------
constexpr float kDashTime  = 0.16f;
constexpr float kDashSpeed = 850.0f;

// ---- match rules -------------------------------------------------------------------------
constexpr float kMatchTime        = 300.0f;  // deathmatch / team deathmatch: 5 minutes
constexpr int   kDmKillLimit      = 25;
constexpr int   kTdmScoreLimit    = 50;
constexpr float kTrainingTime     = 60.0f;
constexpr int   kSurvivalLives    = 3;
constexpr float kWaveBreakTime    = 4.0f;
constexpr float kTrainingRespawn  = 0.35f;  // pause between training targets

// ---- grenades, barrels, power-ups -----------------------------------------------------------
constexpr float kGrenadeFuse       = 1.5f;
constexpr float kGrenadeFriction   = 2.2f;    // velocity *= exp(-friction * t)
constexpr float kGrenadeMaxRange   = 700.0f;
constexpr int   kGrenadeDamage     = 110;
constexpr int   kBarrelHealth      = 30;
constexpr int   kBarrelDamage      = 120;
constexpr float kBarrelRespawn     = 25.0f;
constexpr float kPowerupRespawn    = 30.0f;

// ---- player weapons (index = gm::WeaponId) ------------------------------------------
//                          name       delay  speed  dmg pel spread  mag reload life  shake
const WeaponDef kWeapons[gm::kWeaponCount] = {
    {"Rifle",   0.11f, 1300.0f,  25, 1, 0.00f, 30, 1.6f, 1.6f, 1.5f},
    {"Shotgun", 0.80f, 1000.0f,  14, 7, 0.38f,  6, 2.0f, 0.45f, 6.0f},
    {"Sniper",  1.10f, 2600.0f, 100, 1, 0.00f,  5, 2.4f, 1.2f, 7.0f},
};

// ---- bot types (index = gm::EntityKind) ----------------------------------------------
struct BotKindDef {
    float    speedMin, speedMax;
    int      health;
    float    radius;
    float    fireMin, fireMax;   // seconds between shots
    float    bulletSpeed;
    int      damage;             // per pellet
    int      pellets;
    float    spread;
    float    bulletLife;
    float    distMin, distMax;   // preferred fighting distance
    float    dashChance;
    int      magSize;
    uint32_t weapon;
};
const BotKindDef kBotKinds[4] = {
    {},  // KIND_PLAYER (unused)
    {220, 270, 100, 18, 0.55f, 1.10f, 750, 5, 1, 0.00f, 1.6f, 200, 480, 0.20f, 20, gm::WEAPON_RIFLE},    // soldier
    {310, 360,  60, 14, 0.35f, 0.65f, 800, 3, 1, 0.00f, 1.2f, 140, 300, 0.35f, 25, gm::WEAPON_RIFLE},    // runner
    {150, 180, 220, 25, 1.20f, 1.80f, 620, 4, 5, 0.50f, 0.7f, 160, 300, 0.05f,  6, gm::WEAPON_SHOTGUN},  // heavy
};

// ---- difficulty (index = gm::Difficulty) ------------------------------------------------
struct DifficultyDef {
    const char* name;
    float speed;      // bot movement speed multiplier
    float fireDelay;  // bot fire delay multiplier
    float aimError;   // bot aim error multiplier
    float damage;     // bot damage multiplier
    float reaction;   // bot reaction time multiplier
};
const DifficultyDef kDifficulties[3] = {
    {"Easy",   0.85f, 1.40f, 1.6f, 0.6f, 1.4f},
    {"Normal", 1.00f, 1.00f, 1.0f, 1.0f, 1.0f},
    {"Hard",   1.12f, 0.75f, 0.6f, 1.4f, 0.7f},
};

const char* const kBotNames[] = {
    "Viper",  "Ghost",  "Rook",   "Blaze",  "Nova",   "Frost",  "Havoc",  "Jinx",
    "Raven",  "Titan",  "Echo",   "Onyx",   "Pixel",  "Rogue",  "Sable",  "Talon",
    "Vex",    "Wraith", "Zephyr", "Kilo",   "Mako",   "Nyx",    "Orbit",  "Pulse",
    "Quake",  "Rift",   "Shade",  "Torque", "Umbra",  "Volt",   "Warden",
};
static_assert(sizeof(kBotNames) / sizeof(kBotNames[0]) >= gm::kMaxEntities - 1,
              "need a name for every bot slot");

float WrapAngle(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

float RotateTowards(float current, float target, float maxStep) {
    float diff = WrapAngle(target - current);
    if (std::fabs(diff) <= maxStep) return target;
    return WrapAngle(current + (diff > 0 ? maxStep : -maxStep));
}

Vec2f Rotate(Vec2f v, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// Segment p0->p1 vs axis aligned box. Returns true and the entry fraction t.
bool SegmentVsBox(Vec2f p0, Vec2f p1, const gm::Obstacle& b, float& tOut) {
    Vec2f d = p1 - p0;
    float tMin = 0.0f, tMax = 1.0f;
    const float lo[2] = {b.x, b.y};
    const float hi[2] = {b.x + b.w, b.y + b.h};
    const float p[2]  = {p0.x, p0.y};
    const float dd[2] = {d.x, d.y};
    for (int axis = 0; axis < 2; ++axis) {
        if (std::fabs(dd[axis]) < 1e-8f) {
            if (p[axis] < lo[axis] || p[axis] > hi[axis]) return false;
        } else {
            float inv = 1.0f / dd[axis];
            float t1 = (lo[axis] - p[axis]) * inv;
            float t2 = (hi[axis] - p[axis]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) return false;
        }
    }
    tOut = tMin;
    return true;
}

// Segment p0->p1 vs circle. Returns true and the first contact fraction t.
bool SegmentVsCircle(Vec2f p0, Vec2f p1, Vec2f center, float radius, float& tOut) {
    Vec2f d = p1 - p0;
    Vec2f f = p0 - center;
    float a = Dot(d, d);
    float c = Dot(f, f) - radius * radius;
    if (c <= 0.0f) { tOut = 0.0f; return true; }  // starts inside
    if (a < 1e-8f) return false;
    float b = 2.0f * Dot(f, d);
    float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return false;
    float t = (-b - std::sqrt(disc)) / (2.0f * a);
    if (t < 0.0f || t > 1.0f) return false;
    tOut = t;
    return true;
}

}  // namespace

const WeaponDef& GetWeaponDef(uint32_t weapon) {
    return kWeapons[weapon < gm::kWeaponCount ? weapon : 0];
}

const char* DifficultyName(uint32_t d) { return kDifficulties[d < 3 ? d : 1].name; }

const char* KindName(uint32_t kind) {
    switch (kind) {
        case gm::KIND_PLAYER:  return "Player";
        case gm::KIND_SOLDIER: return "Soldier";
        case gm::KIND_RUNNER:  return "Runner";
        case gm::KIND_HEAVY:   return "Heavy";
        default:               return "?";
    }
}

const char* ModeName(uint32_t mode) {
    switch (mode) {
        case gm::MODE_DEATHMATCH:      return "Deathmatch";
        case gm::MODE_TEAM_DEATHMATCH: return "Team Deathmatch";
        case gm::MODE_SURVIVAL:        return "Survival";
        case gm::MODE_TRAINING:        return "Aim Training";
        default:                       return "?";
    }
}

const char* ModeDescription(uint32_t mode) {
    switch (mode) {
        case gm::MODE_DEATHMATCH:      return "Everyone is your enemy. First to 25 kills, 5 minutes.";
        case gm::MODE_TEAM_DEATHMATCH: return "You + allied bots vs enemy bots. First team to 50 kills.";
        case gm::MODE_SURVIVAL:        return "Endless waves that get harder. 3 lives, no enemy respawns.";
        case gm::MODE_TRAINING:        return "60 s, one moving target at a time. Measures accuracy, reaction and time to kill.";
        default:                       return "";
    }
}

// =============================================================================

Game::Game(gm::GameMemory& mem, uint32_t seed) : mem_(mem), rng_(seed) {
    StartMatch(MatchSettings{});
}

float Game::RandF(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng_);
}

int Game::RandI(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng_);
}

std::vector<GameEvent> Game::TakeEvents() {
    std::vector<GameEvent> out;
    out.swap(events_);
    return out;
}

void Game::SetDifficulty(uint32_t d) {
    mem_.difficulty = d > gm::DIFFICULTY_HARD ? gm::DIFFICULTY_NORMAL : d;
    settings_.difficulty = mem_.difficulty;
}

int Game::KillLimit() const {
    switch (settings_.mode) {
        case gm::MODE_DEATHMATCH:      return kDmKillLimit;
        case gm::MODE_TEAM_DEATHMATCH: return kTdmScoreLimit;
        default:                       return 0;
    }
}

void Game::BuildArena(int map) {
    const MapDef& def = GetMap(map);
    mem_.arenaSize = {kArenaW, kArenaH};
    mem_.obstacleCount = 0;
    for (const auto& o : def.walls) {
        if (mem_.obstacleCount >= static_cast<uint32_t>(gm::kMaxObstacles)) break;
        mem_.obstacles[mem_.obstacleCount++] = o;
    }
    std::memset(mem_.obstacles + mem_.obstacleCount, 0,
                sizeof(gm::Obstacle) * (gm::kMaxObstacles - mem_.obstacleCount));

    std::memset(mem_.pickups, 0, sizeof(mem_.pickups));
    int i = 0;
    for (Vec2f p : def.healthPacks) {
        if (i >= gm::kMaxPickups) break;
        mem_.pickups[i].pos = p;
        mem_.pickups[i].type = gm::PICKUP_HEALTH;
        mem_.pickups[i].available = 1;
        pickupTimers_[i] = 0.0f;
        ++i;
    }
    for (Vec2f p : def.powerups) {
        if (i >= gm::kMaxPickups) break;
        mem_.pickups[i].pos = p;
        mem_.pickups[i].type = RandomPowerup();
        mem_.pickups[i].available = 1;
        pickupTimers_[i] = 0.0f;
        ++i;
    }

    std::memset(mem_.barrels, 0, sizeof(mem_.barrels));
    int b = 0;
    for (Vec2f p : def.barrels) {
        if (b >= gm::kMaxBarrels) break;
        mem_.barrels[b].pos = p;
        mem_.barrels[b].health = kBarrelHealth;
        mem_.barrels[b].alive = 1;
        barrelTimers_[b] = 0.0f;
        barrelPending_[b] = false;
        barrelAttacker_[b] = -1;
        ++b;
    }
    std::memset(mem_.grenades, 0, sizeof(mem_.grenades));
}

uint32_t Game::RandomPowerup() {
    static const uint32_t types[] = {gm::PICKUP_SPEED, gm::PICKUP_DAMAGE, gm::PICKUP_SHIELD, gm::PICKUP_GRENADE};
    return types[RandI(0, 3)];
}

// Which team does entity slot `slot` play for in the current mode?
static uint32_t TeamForSlot(uint32_t mode, int slot, int bots) {
    if (slot == 0) return gm::TEAM_PLAYER;
    if (mode == gm::MODE_TEAM_DEATHMATCH) {
        int allies = (bots - 1) / 2;  // e.g. 8 bots -> 3 allies: you + 3 vs 5
        return slot <= allies ? gm::TEAM_PLAYER : gm::TEAM_BOTS;
    }
    return gm::TEAM_BOTS;
}

void Game::StartMatch(const MatchSettings& settings) {
    settings_ = settings;
    settings_.map = std::max(0, std::min(settings_.map, MapCount() - 1));
    settings_.bots = std::max(0, std::min(settings_.bots, gm::kMaxEntities - 1));
    mem_.difficulty = settings_.difficulty <= gm::DIFFICULTY_HARD ? settings_.difficulty : gm::DIFFICULTY_NORMAL;
    BuildArena(settings_.map);

    bullets_.clear();
    effects_.clear();
    texts_.clear();
    killFeed_.clear();
    events_.clear();
    shotsFired = shotsHit = 0;
    killStreak_ = multiKill_ = 0;
    announcement_ = Announcement{};
    training_ = TrainingStats{};
    result_ = MatchResult{};
    matchTime_ = 0.0f;
    waveBreak_ = 0.0f;
    dashTimer_ = dashCooldown_ = 0.0f;
    damageIndicators_.clear();
    deathRecap_.clear();
    std::memset(mem_.buffs, 0, sizeof(mem_.buffs));

    std::memset(&mem_.match, 0, sizeof(mem_.match));
    mem_.match.mode  = settings_.mode;
    mem_.match.map   = static_cast<uint32_t>(settings_.map);
    mem_.match.state = gm::MATCH_PLAYING;
    mem_.localPlayerIndex = 0;

    for (int i = 0; i < gm::kMaxEntities; ++i) {
        gm::Entity& e = mem_.entities[i];
        std::memset(&e, 0, sizeof(e));
        e.team = TeamForSlot(settings_.mode, i, settings_.bots);
        const char* name = (i == 0) ? "You" : kBotNames[i - 1];
        std::snprintf(e.name, sizeof(e.name), "%s", name);
    }

    // The player first, then bots (they spawn away from their enemies).
    gm::Entity& p = mem_.entities[0];
    p.active = 1;
    p.kind = gm::KIND_PLAYER;
    p.weapon = gm::WEAPON_RIFLE;
    SpawnEntity(0);
    botCount_ = 0;

    switch (settings_.mode) {
        case gm::MODE_DEATHMATCH:
        case gm::MODE_TEAM_DEATHMATCH:
            mem_.match.timeLeft = kMatchTime;
            SetBotCount(settings_.bots);
            break;
        case gm::MODE_SURVIVAL:
            mem_.match.livesLeft = kSurvivalLives;
            mem_.match.wave = 0;
            waveBreak_ = 2.5f;  // first wave after a short delay
            mem_.entityCount = 1;
            break;
        case gm::MODE_TRAINING:
            mem_.match.timeLeft = kTrainingTime;
            training_.nextSpawn = 1.0f;
            mem_.entityCount = 2;
            break;
    }
}

void Game::EnterMenu(int map) {
    BuildArena(std::max(0, std::min(map, MapCount() - 1)));
    bullets_.clear();
    effects_.clear();
    texts_.clear();
    killFeed_.clear();
    events_.clear();
    for (int i = 0; i < gm::kMaxEntities; ++i) {
        mem_.entities[i].active = 0;
        mem_.entities[i].alive = 0;
    }
    mem_.entityCount = 0;
    std::memset(&mem_.match, 0, sizeof(mem_.match));
    mem_.match.state = gm::MATCH_MENU;
    mem_.match.map = static_cast<uint32_t>(map);
}

void Game::SetBotCount(int count) {
    // Only deathmatch modes let you change the bot count during the match.
    if (settings_.mode != gm::MODE_DEATHMATCH && settings_.mode != gm::MODE_TEAM_DEATHMATCH) return;
    count = std::max(0, std::min(count, gm::kMaxEntities - 1));
    botCount_ = count;
    settings_.bots = count;
    for (int i = 1; i < gm::kMaxEntities; ++i) {
        gm::Entity& e = mem_.entities[i];
        bool shouldBeActive = i <= count;
        uint32_t team = TeamForSlot(settings_.mode, i, count);
        if (shouldBeActive && !e.active) {
            e.active = 1;
            e.team = team;
            e.kills = e.deaths = 0;
            SpawnEntity(i);
        } else if (!shouldBeActive && e.active) {
            e.active = 0;
            e.alive = 0;
            e.health = 0;
        } else if (shouldBeActive) {
            e.team = team;  // team sizes may change with the bot count
        }
    }
    mem_.entityCount = static_cast<uint32_t>(count + 1);
}

bool Game::CircleHitsObstacle(Vec2f pos, float radius) const {
    for (uint32_t i = 0; i < mem_.obstacleCount; ++i) {
        const gm::Obstacle& o = mem_.obstacles[i];
        float cx = Clampf(pos.x, o.x, o.x + o.w);
        float cy = Clampf(pos.y, o.y, o.y + o.h);
        float dx = pos.x - cx, dy = pos.y - cy;
        if (dx * dx + dy * dy < radius * radius) return true;
    }
    return false;
}

// A free spot far away from the enemies of `team`.
Vec2f Game::FindSpawnPoint(uint32_t team) {
    const float margin = 60.0f;
    Vec2f best{kArenaW * 0.5f, kArenaH * 0.25f};
    float bestScore = -1.0f;
    for (int attempt = 0; attempt < 60; ++attempt) {
        Vec2f p{RandF(margin, kArenaW - margin), RandF(margin, kArenaH - margin)};
        if (CircleHitsObstacle(p, 40.0f)) continue;

        float nearest = 1e9f;
        for (int i = 0; i < gm::kMaxEntities; ++i) {
            const gm::Entity& e = mem_.entities[i];
            if (!e.active || !e.alive || e.team == team) continue;
            nearest = std::min(nearest, Distance(p, e.pos));
        }
        if (nearest > 650.0f) return p;
        if (nearest > bestScore) {
            bestScore = nearest;
            best = p;
        }
    }
    return best;
}

// Training targets spawn where you can see them: on screen, with line of sight.
Vec2f Game::FindTrainingSpawn() {
    const gm::Entity& p = Player();
    for (int attempt = 0; attempt < 200; ++attempt) {
        float a = RandF(-kPi, kPi);
        float d = RandF(300.0f, 650.0f);
        Vec2f pos = p.pos + Vec2f{std::cos(a) * d, std::sin(a) * d * 0.6f};
        if (pos.x < 60 || pos.y < 60 || pos.x > kArenaW - 60 || pos.y > kArenaH - 60) continue;
        if (CircleHitsObstacle(pos, 30.0f) || !LineOfSight(p.pos, pos)) continue;
        return pos;
    }
    return FindSpawnPoint(gm::TEAM_BOTS);
}

void Game::SpawnEntity(int slot) {
    gm::Entity& e = mem_.entities[slot];
    bool isPlayer = slot == static_cast<int>(mem_.localPlayerIndex);

    e.id           = nextId_++;
    e.alive        = 1;
    e.vel          = {0, 0};
    e.aimAngle     = RandF(-kPi, kPi);
    e.respawnTimer = 0.0f;
    e.reloadTimer  = 0.0f;
    e.visible      = 0;
    hitFlash_[slot] = 0.0f;
    gm::EntityBuffs& buff = mem_.buffs[slot];
    buff.speedTime = buff.damageTime = 0.0f;
    buff.shield = 0;
    buff.grenades = isPlayer ? 2 : 1;

    if (isPlayer) {
        e.pos       = FindSpawnPoint(e.team);
        e.kind      = gm::KIND_PLAYER;
        e.maxHealth = kPlayerMaxHealth;
        e.radius    = kPlayerRadius;
        for (int w = 0; w < gm::kWeaponCount; ++w) playerAmmo_[w] = kWeapons[w].magSize;
        e.ammo = playerAmmo_[e.weapon];
        playerFireCooldown_ = 0.0f;
        spawnProtection_ = kSpawnProtection;
        killStreak_ = 0;
        Emit(GameEventType::Respawn, e.pos);
    } else {
        // Pick a bot type. Survival gets more heavies in later waves,
        // training only uses targets that move a lot.
        float r = RandF(0, 1);
        if (settings_.mode == gm::MODE_TRAINING) {
            e.kind = r < 0.6f ? gm::KIND_SOLDIER : gm::KIND_RUNNER;
        } else {
            float heavy = 0.15f;
            if (settings_.mode == gm::MODE_SURVIVAL) heavy = std::min(0.05f + 0.03f * mem_.match.wave, 0.35f);
            e.kind = r < heavy ? gm::KIND_HEAVY : (r < heavy + 0.25f ? gm::KIND_RUNNER : gm::KIND_SOLDIER);
        }
        const BotKindDef& k = kBotKinds[e.kind];
        e.maxHealth = settings_.mode == gm::MODE_TRAINING ? 100 : k.health;  // same health = fair timing
        e.radius    = k.radius;
        e.weapon    = k.weapon;
        e.ammo      = k.magSize;
        e.pos       = settings_.mode == gm::MODE_TRAINING ? FindTrainingSpawn() : FindSpawnPoint(e.team);

        BotBrain& b = brains_[slot];
        b = BotBrain{};
        b.baseSpeed     = RandF(k.speedMin, k.speedMax);
        b.strafeDir     = RandF(0, 1) < 0.5f ? -1.0f : 1.0f;
        b.preferredDist = RandF(k.distMin, k.distMax);
        b.fireCooldown  = RandF(0.5f, 1.2f);
        b.wanderTarget  = FindSpawnPoint(e.team);
    }
    e.health = e.maxHealth;
}

// -----------------------------------------------------------------------------
//  Collision helpers
// -----------------------------------------------------------------------------

Vec2f Game::MoveCircle(Vec2f& pos, Vec2f delta, float radius) const {
    Vec2f start = pos;
    pos += delta;

    for (int iteration = 0; iteration < 3; ++iteration) {
        for (uint32_t i = 0; i < mem_.obstacleCount; ++i) {
            const gm::Obstacle& o = mem_.obstacles[i];
            float cx = Clampf(pos.x, o.x, o.x + o.w);
            float cy = Clampf(pos.y, o.y, o.y + o.h);
            Vec2f diff{pos.x - cx, pos.y - cy};
            float distSq = LengthSq(diff);
            if (distSq >= radius * radius) continue;

            if (distSq > 1e-6f) {
                float dist = std::sqrt(distSq);
                pos += diff / dist * (radius - dist);
            } else {
                // Center is inside the box: push out along the shortest axis.
                float left   = pos.x - o.x;
                float right  = o.x + o.w - pos.x;
                float top    = pos.y - o.y;
                float bottom = o.y + o.h - pos.y;
                float m = std::min(std::min(left, right), std::min(top, bottom));
                if (m == left)        pos.x = o.x - radius;
                else if (m == right)  pos.x = o.x + o.w + radius;
                else if (m == top)    pos.y = o.y - radius;
                else                  pos.y = o.y + o.h + radius;
            }
        }
    }
    // Barrels are round obstacles.
    for (int i = 0; i < gm::kMaxBarrels; ++i) {
        const gm::Barrel& b = mem_.barrels[i];
        if (!b.alive) continue;
        Vec2f diff = pos - b.pos;
        float minDist = radius + kBarrelRadius;
        float d = Length(diff);
        if (d < minDist && d > 1e-4f) pos += diff / d * (minDist - d);
    }
    pos.x = Clampf(pos.x, radius, kArenaW - radius);
    pos.y = Clampf(pos.y, radius, kArenaH - radius);
    return pos - start;
}

bool Game::LineOfSight(Vec2f a, Vec2f b) const {
    for (uint32_t i = 0; i < mem_.obstacleCount; ++i) {
        float t;
        if (SegmentVsBox(a, b, mem_.obstacles[i], t)) return false;
    }
    return true;
}

int Game::NearestPickup(Vec2f from, float maxDist) const {
    int best = -1;
    float bestDist = maxDist;
    for (int i = 0; i < gm::kMaxPickups; ++i) {
        const gm::Pickup& p = mem_.pickups[i];
        if (!p.available) continue;
        float d = Distance(from, p.pos);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

// -----------------------------------------------------------------------------
//  Main tick
// -----------------------------------------------------------------------------

void Game::Tick(const PlayerInput& input) {
    const float dt = kTickDt;
    if (mem_.match.state != gm::MATCH_PLAYING) {
        UpdateEffects(dt);  // let particles finish on the result screen
        return;
    }
    mem_.gameTime += dt;
    matchTime_ += dt;

    UpdatePlayer(input, dt);
    for (int i = 1; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = mem_.entities[i];
        if (e.active && e.alive) UpdateBot(i, dt);
    }
    UpdateBullets(dt);
    UpdateGrenades(dt);
    UpdateBarrels(dt);
    UpdatePickups(dt);
    UpdateBuffs(dt);
    UpdateRespawns(dt);
    UpdateEffects(dt);
    UpdateVisibility();
    UpdateMatch(dt);

    hitMarker_       = std::max(0.0f, hitMarker_ - dt);
    damageFlash_     = std::max(0.0f, damageFlash_ - dt);
    spawnProtection_ = std::max(0.0f, spawnProtection_ - dt);
    multiKillTimer_  = std::max(0.0f, multiKillTimer_ - dt);
    announcement_.time += dt;
    for (float& f : hitFlash_) f = std::max(0.0f, f - dt);
}

void Game::UpdateVisibility() {
    const gm::Entity& p = Player();
    for (int i = 0; i < gm::kMaxEntities; ++i) {
        gm::Entity& e = mem_.entities[i];
        if (i == static_cast<int>(mem_.localPlayerIndex)) { e.visible = 1; continue; }
        e.visible = (e.active && e.alive && LineOfSight(p.pos, e.pos)) ? 1u : 0u;
    }
}

// -----------------------------------------------------------------------------
//  Match rules
// -----------------------------------------------------------------------------

void Game::StartWave(int wave) {
    mem_.match.wave = static_cast<uint32_t>(wave);
    int count = std::min(2 + 2 * wave, gm::kMaxEntities - 1);
    botCount_ = count;
    for (int i = 1; i < gm::kMaxEntities; ++i) {
        gm::Entity& e = mem_.entities[i];
        e.active = i <= count ? 1u : 0u;
        e.alive = 0;
        e.team = gm::TEAM_BOTS;
        if (e.active) SpawnEntity(i);
    }
    mem_.entityCount = static_cast<uint32_t>(count + 1);
    Announce("WAVE " + std::to_string(wave));
    Emit(GameEventType::WaveStart, Player().pos);
}

void Game::UpdateMatch(float dt) {
    gm::MatchInfo& m = mem_.match;

    // ---- time limit ----
    if (m.timeLeft > 0.0f) {
        m.timeLeft -= dt;
        if (m.timeLeft <= 0.0f) {
            m.timeLeft = 0.0f;
            const gm::Entity& p = Player();
            if (settings_.mode == gm::MODE_TRAINING) {
                EndMatch("TRAINING COMPLETE", true);
            } else if (settings_.mode == gm::MODE_TEAM_DEATHMATCH) {
                if (m.teamScore[0] == m.teamScore[1]) EndMatch("DRAW", false);
                else EndMatch(m.teamScore[0] > m.teamScore[1] ? "VICTORY" : "DEFEAT", m.teamScore[0] > m.teamScore[1]);
            } else {
                int best = 0;
                for (int i = 1; i < gm::kMaxEntities; ++i)
                    if (mem_.entities[i].active) best = std::max(best, mem_.entities[i].kills);
                EndMatch(p.kills > best ? "VICTORY" : (p.kills == best ? "DRAW" : "DEFEAT"), p.kills > best);
            }
            return;
        }
    }

    // ---- survival waves ----
    if (settings_.mode == gm::MODE_SURVIVAL) {
        if (waveBreak_ > 0.0f) {
            waveBreak_ -= dt;
            if (waveBreak_ <= 0.0f) StartWave(static_cast<int>(m.wave) + 1);
            return;
        }
        int alive = 0;
        for (int i = 1; i < gm::kMaxEntities; ++i) alive += mem_.entities[i].active && mem_.entities[i].alive;
        if (alive == 0 && m.wave > 0) {
            waveBreak_ = kWaveBreakTime;
            Announce("WAVE " + std::to_string(m.wave) + " CLEARED");
            // reward: refill health a bit
            gm::Entity& p = Player();
            if (p.alive) p.health = std::min(p.maxHealth, p.health + 25);
        }
    }

    // ---- aim training: one target at a time ----
    if (settings_.mode == gm::MODE_TRAINING) {
        gm::Entity& t = mem_.entities[1];
        if (t.active && t.alive) {
            training_.targetAge += dt;
        } else {
            training_.nextSpawn -= dt;
            if (training_.nextSpawn <= 0.0f) {
                t.active = 1;
                t.team = gm::TEAM_BOTS;
                SpawnEntity(1);
                training_.targetsSpawned++;
                training_.targetAge = 0.0f;
                training_.targetHit = false;
            }
        }
    }
}

void Game::OnKill(int victim, int attacker) {
    gm::MatchInfo& m = mem_.match;
    const gm::Entity& v = mem_.entities[victim];
    bool victimIsPlayer = victim == static_cast<int>(mem_.localPlayerIndex);

    if (attacker >= 0 && mem_.entities[attacker].team != v.team) {
        m.teamScore[mem_.entities[attacker].team == gm::TEAM_PLAYER ? 0 : 1]++;
    }

    switch (settings_.mode) {
        case gm::MODE_DEATHMATCH:
            if (attacker >= 0 && mem_.entities[attacker].kills >= kDmKillLimit) {
                bool won = attacker == static_cast<int>(mem_.localPlayerIndex);
                EndMatch(won ? "VICTORY" : "DEFEAT", won);
            }
            break;
        case gm::MODE_TEAM_DEATHMATCH:
            if (m.teamScore[0] >= kTdmScoreLimit) EndMatch("VICTORY", true);
            else if (m.teamScore[1] >= kTdmScoreLimit) EndMatch("DEFEAT", false);
            break;
        case gm::MODE_SURVIVAL:
            if (victimIsPlayer) {
                m.livesLeft--;
                if (m.livesLeft <= 0) EndMatch("GAME OVER", false);
            }
            break;
        case gm::MODE_TRAINING:
            if (victim == 1) {
                training_.kills++;
                training_.totalTimeToKill += training_.targetAge;
                if (training_.bestTimeToKill <= 0.0f || training_.targetAge < training_.bestTimeToKill)
                    training_.bestTimeToKill = training_.targetAge;
                training_.nextSpawn = kTrainingRespawn;
                mem_.entities[1].respawnTimer = 0.0f;
            }
            break;
    }
}

void Game::EndMatch(const std::string& title, bool good) {
    if (mem_.match.state == gm::MATCH_ENDED) return;
    mem_.match.state = gm::MATCH_ENDED;
    result_ = MatchResult{};
    result_.title = title;
    result_.good = good;

    const gm::Entity& p = Player();
    float acc = shotsFired > 0 ? 100.0f * shotsHit / shotsFired : 0.0f;
    int secs = static_cast<int>(matchTime_);
    char buf[160];
    auto line = [&](const char* fmt, auto... args) {
        std::snprintf(buf, sizeof(buf), fmt, args...);
        result_.lines.push_back(buf);
    };

    line("%s  -  %s  -  %s", ModeName(settings_.mode), GetMap(settings_.map).name, DifficultyName(mem_.difficulty));
    line("Time played\t%d:%02d", secs / 60, secs % 60);

    float avgTtk = 0.0f, avgReaction = 0.0f;
    switch (settings_.mode) {
        case gm::MODE_DEATHMATCH: {
            int place = 1;
            for (int i = 1; i < gm::kMaxEntities; ++i)
                if (mem_.entities[i].active && mem_.entities[i].kills > p.kills) ++place;
            line("Place\t#%d of %d", place, botCount_ + 1);
            break;
        }
        case gm::MODE_TEAM_DEATHMATCH:
            line("Team score\t%d : %d", mem_.match.teamScore[0], mem_.match.teamScore[1]);
            break;
        case gm::MODE_SURVIVAL:
            line("Waves survived\t%d", std::max(0, static_cast<int>(mem_.match.wave) - 1));
            break;
        case gm::MODE_TRAINING:
            avgTtk = training_.kills > 0 ? training_.totalTimeToKill / training_.kills : 0.0f;
            avgReaction = training_.reactions > 0 ? training_.totalReaction / training_.reactions : 0.0f;
            line("Targets killed\t%d", training_.kills);
            line("Kills / minute\t%.1f", training_.kills * 60.0f / std::max(1.0f, matchTime_));
            line("Avg time to kill\t%.0f ms   (best %.0f ms)", avgTtk * 1000.0f, training_.bestTimeToKill * 1000.0f);
            line("Avg first hit\t%.0f ms   (spawn -> first hit)", avgReaction * 1000.0f);
            break;
    }
    line("Kills / deaths\t%d / %d", p.kills, p.deaths);
    line("Accuracy\t%.1f%%   (%d / %d)", acc, shotsHit, shotsFired);

    // One CSV line per match -> easy to compare runs with and without aimbot.
    char date[32];
    std::time_t now = std::time(nullptr);
    std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    char csv[512];
    std::snprintf(csv, sizeof(csv), "%s,%s,%s,%s,%d,%.1f,%d,%d,%d,%d,%.1f,%.0f,%.0f,%u,%s", date,
                  ModeName(settings_.mode), GetMap(settings_.map).name, DifficultyName(mem_.difficulty), botCount_,
                  matchTime_, p.kills, p.deaths, shotsFired, shotsHit, acc, avgTtk * 1000.0f, avgReaction * 1000.0f,
                  mem_.match.wave, title.c_str());
    result_.csvLine = csv;
    Emit(GameEventType::MatchEnd, p.pos);
}

// -----------------------------------------------------------------------------
//  Player
// -----------------------------------------------------------------------------

void Game::UpdatePlayer(const PlayerInput& input, float dt) {
    gm::Entity& p = Player();
    playerFireCooldown_ -= dt;
    dashCooldown_ = std::max(0.0f, dashCooldown_ - dt);
    if (!p.alive) return;

    // ---- weapon switching ----------------------------------------------------
    if (input.selectWeapon >= 0 && input.selectWeapon < gm::kWeaponCount &&
        static_cast<uint32_t>(input.selectWeapon) != p.weapon) {
        p.weapon = static_cast<uint32_t>(input.selectWeapon);
        p.reloadTimer = 0.0f;  // switching cancels a reload
        playerFireCooldown_ = std::max(playerFireCooldown_, 0.25f);
        Emit(GameEventType::WeaponSwitch, p.pos, p.weapon);
    }
    const WeaponDef& w = kWeapons[p.weapon];
    mem_.bulletSpeed = w.bulletSpeed;  // <- the aimbot uses this for prediction

    // ---- reloading ----------------------------------------------------------------
    int& ammo = playerAmmo_[p.weapon];
    if (p.reloadTimer > 0.0f) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0.0f) {
            p.reloadTimer = 0.0f;
            ammo = w.magSize;
            Emit(GameEventType::ReloadDone, p.pos, p.weapon);
        }
    } else if (input.reload && ammo < w.magSize) {
        p.reloadTimer = w.reloadTime;
        Emit(GameEventType::Reload, p.pos, p.weapon);
    }

    // ---- movement (+ dash) -------------------------------------------------------------
    if (input.dash && dashCooldown_ <= 0.0f) {
        Vec2f dir = LengthSq(input.move) > 0.01f ? Normalize(input.move) : FromAngle(p.aimAngle);
        dashDir_ = dir;
        dashTimer_ = kDashTime;
        dashCooldown_ = kDashCooldown;
        Emit(GameEventType::Dash, p.pos);
    }
    if (dashTimer_ > 0.0f) {
        dashTimer_ -= dt;
        p.vel = dashDir_ * kDashSpeed;
    } else {
        float speed = kPlayerSpeed * (mem_.buffs[0].speedTime > 0.0f ? 1.4f : 1.0f);
        Vec2f wish = Normalize(input.move) * speed;
        p.vel = MoveTowards(p.vel, wish, kPlayerAccel * dt);
    }
    Vec2f moved = MoveCircle(p.pos, p.vel * dt, p.radius);
    // Store the real velocity (blocked by walls = slower), like the bots do.
    p.vel = moved / dt;

    Vec2f toAim = input.aimWorld - p.pos;
    if (LengthSq(toAim) > 1.0f) p.aimAngle = AngleOf(toAim);

    if (input.throwGrenade && ThrowGrenade(0, input.aimWorld)) spawnProtection_ = 0.0f;

    // ---- shooting --------------------------------------------------------------------
    if (input.shoot && playerFireCooldown_ <= 0.0f && p.reloadTimer <= 0.0f) {
        if (ammo > 0) {
            FireBullets(0, p.aimAngle, w.bulletSpeed, w.damage, w.pellets, w.spread, w.bulletLife,
                        p.weapon == gm::WEAPON_SNIPER);
            --ammo;
            playerFireCooldown_ = w.fireDelay;
            spawnProtection_ = 0.0f;  // shooting ends spawn protection
            ++shotsFired;
            Emit(GameEventType::PlayerShot, p.pos, p.weapon);
            if (ammo == 0) {  // auto reload
                p.reloadTimer = w.reloadTime;
                Emit(GameEventType::Reload, p.pos, p.weapon);
            }
        } else {
            playerFireCooldown_ = 0.25f;
            Emit(GameEventType::Empty, p.pos, p.weapon);
            p.reloadTimer = w.reloadTime;
            Emit(GameEventType::Reload, p.pos, p.weapon);
        }
    }
    p.ammo = ammo;
}

// -----------------------------------------------------------------------------
//  Bots
// -----------------------------------------------------------------------------

// Nearest enemy of `slot` that it can see (-1 = none).
int Game::FindBotTarget(int slot, bool& canSee) {
    const gm::Entity& bot = mem_.entities[slot];
    int best = -1;
    float bestDist = kBotViewRange;
    for (int i = 0; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = mem_.entities[i];
        if (i == slot || !e.active || !e.alive || e.team == bot.team) continue;
        float d = Distance(bot.pos, e.pos);
        if (d < bestDist && LineOfSight(bot.pos, e.pos)) {
            bestDist = d;
            best = i;
        }
    }
    canSee = best >= 0;
    return best;
}

void Game::UpdateBot(int slot, float dt) {
    gm::Entity& bot = mem_.entities[slot];
    BotBrain&   b   = brains_[slot];
    const BotKindDef& kind = kBotKinds[bot.kind <= gm::KIND_HEAVY ? bot.kind : gm::KIND_SOLDIER];
    const DifficultyDef& diff = kDifficulties[mem_.difficulty <= 2 ? mem_.difficulty : 1];
    const bool training = settings_.mode == gm::MODE_TRAINING;

    b.decisionTimer -= dt;
    b.dashTimer     -= dt;
    b.dashCooldown  -= dt;
    b.fireCooldown  -= dt;
    b.reaction      -= dt;
    b.lastSeenTimer += dt;
    b.retargetTimer -= dt;
    b.grenadeCooldown -= dt;

    if (bot.reloadTimer > 0.0f) {
        bot.reloadTimer -= dt;
        if (bot.reloadTimer <= 0.0f) {
            bot.reloadTimer = 0.0f;
            bot.ammo = kind.magSize;
        }
    }

    // ---- who are we fighting? -----------------------------------------------------
    if (b.retargetTimer <= 0.0f) {
        b.retargetTimer = RandF(0.2f, 0.35f);
        bool canSee = false;
        int t = FindBotTarget(slot, canSee);
        if (canSee && t != b.target) {
            b.target = t;
            b.lastSeenTimer = 99.0f;  // counts as "just spotted" below
        }
    }
    const gm::Entity* tgt = nullptr;
    if (b.target >= 0) {
        const gm::Entity& t = mem_.entities[b.target];
        if (t.active && t.alive && t.team != bot.team) tgt = &t;
        else b.target = -1;
    }

    float dist = 1e9f;
    bool seesTarget = false;
    if (tgt) {
        dist = Distance(bot.pos, tgt->pos);
        seesTarget = dist < kBotViewRange && LineOfSight(bot.pos, tgt->pos);
    }
    if (seesTarget) {
        if (b.lastSeenTimer > 1.0f) b.reaction = RandF(0.25f, 0.5f) * diff.reaction;  // just spotted
        b.lastSeenTimer = 0.0f;
        b.lastSeenPos = tgt->pos;
    }
    bool engaging = tgt && b.lastSeenTimer < 3.0f;

    // Target hiding behind cover? Throw a grenade where it was last seen.
    if (engaging && !seesTarget && !training && !botsPeaceful && b.grenadeCooldown <= 0.0f &&
        b.lastSeenTimer > 0.4f && b.lastSeenTimer < 2.5f && bot.kind != gm::KIND_RUNNER &&
        Distance(bot.pos, b.lastSeenPos) < 650.0f && Distance(bot.pos, b.lastSeenPos) > 200.0f) {
        if (ThrowGrenade(slot, b.lastSeenPos + Vec2f{RandF(-40, 40), RandF(-40, 40)})) b.grenadeCooldown = RandF(8.0f, 14.0f);
        else b.grenadeCooldown = 3.0f;
    }

    // Low on health? Go for a health pack.
    int pack = -1;
    if (!training && bot.health < bot.maxHealth * 45 / 100) pack = NearestPickup(bot.pos, 1000.0f);

    // ---- movement decision -------------------------------------------------
    if (b.decisionTimer <= 0.0f) {
        b.decisionTimer = RandF(0.25f, 0.8f);
        if (RandF(0, 1) < 0.65f) b.strafeDir = -b.strafeDir;  // change direction a lot
        b.preferredDist = RandF(kind.distMin, kind.distMax);
        b.jitterAngle   = RandF(-0.6f, 0.6f);

        // Is our target aiming at us? -> more likely to dodge with a dash.
        float dashChance = kind.dashChance;
        if (tgt && seesTarget) {
            float aimDiff = std::fabs(WrapAngle(tgt->aimAngle - AngleOf(bot.pos - tgt->pos)));
            if (aimDiff < 0.2f) dashChance += 0.35f;
        }
        if (b.dashCooldown <= 0.0f && RandF(0, 1) < dashChance) {
            b.dashTimer    = RandF(0.15f, 0.3f);
            b.dashCooldown = RandF(0.8f, 1.6f);
        }
        if (!engaging && Distance(bot.pos, b.wanderTarget) < 80.0f) {
            b.wanderTarget = FindSpawnPoint(bot.team);
        }
    }

    Vec2f desired{0, 0};
    if (pack >= 0) {
        // Run to the health pack (still shooting if the target is visible).
        Vec2f to = mem_.pickups[pack].pos - bot.pos;
        desired = Normalize(Rotate(to, b.jitterAngle * 0.3f));
    } else if (engaging) {
        Vec2f target = seesTarget ? tgt->pos : b.lastSeenPos;
        Vec2f to = target - bot.pos;
        float d = Length(to);
        Vec2f dir = Normalize(to);
        if (seesTarget) {
            // Circle around the target at the preferred distance.
            // While reloading, back off a bit.
            float wanted = b.preferredDist + (bot.reloadTimer > 0.0f ? 200.0f : 0.0f);
            float radial = Clampf((d - wanted) / 150.0f, -1.0f, 1.0f);
            Vec2f tangent = Perp(dir) * b.strafeDir;
            desired = Normalize(Rotate(dir * radial + tangent, b.jitterAngle));
        } else {
            // Hunt: walk to where the target was last seen.
            desired = d > 30.0f ? Rotate(dir, b.jitterAngle * 0.5f) : Vec2f{0, 0};
        }
    } else if (training) {
        // Training targets strafe around where they are.
        desired = Normalize(Perp(Normalize(Player().pos - bot.pos)) * b.strafeDir);
    } else {
        // Nothing to fight: grab a nearby power-up, otherwise wander.
        for (int i = 0; i < gm::kMaxPickups; ++i) {
            const gm::Pickup& pk = mem_.pickups[i];
            if (pk.available && pk.type != gm::PICKUP_HEALTH && Distance(bot.pos, pk.pos) < 600.0f) {
                b.wanderTarget = pk.pos;
                break;
            }
        }
        desired = Normalize(Rotate(b.wanderTarget - bot.pos, b.jitterAngle * 0.3f));
    }

    // Keep some distance to other bots.
    for (int j = 1; j < gm::kMaxEntities; ++j) {
        if (j == slot) continue;
        const gm::Entity& o = mem_.entities[j];
        if (!o.active || !o.alive) continue;
        Vec2f away = bot.pos - o.pos;
        float d = Length(away);
        float minDist = bot.radius + o.radius + 34.0f;
        if (d < minDist && d > 1e-3f) desired += away / d * ((minDist - d) / minDist) * 1.5f;
    }
    desired = Normalize(desired);

    float speed = b.baseSpeed * diff.speed * (b.dashTimer > 0.0f ? 2.3f : 1.0f);
    if (mem_.buffs[slot].speedTime > 0.0f) speed *= 1.4f;
    if (botsFrozen) speed = 0.0f;

    bot.vel = MoveTowards(bot.vel, desired * speed, kBotAccel * dt);
    Vec2f expected = bot.vel * dt;
    Vec2f moved = MoveCircle(bot.pos, expected, bot.radius);
    bot.vel = moved / dt;

    // Stuck on a wall? Pick another direction.
    if (LengthSq(expected) > 0.25f && LengthSq(moved) < LengthSq(expected) * 0.2f) {
        b.stuckTimer += dt;
        if (b.stuckTimer > 0.12f) {
            b.stuckTimer    = 0.0f;
            b.strafeDir     = -b.strafeDir;
            b.jitterAngle   = RandF(-1.2f, 1.2f);
            b.decisionTimer = RandF(0.3f, 0.6f);
            b.wanderTarget  = FindSpawnPoint(bot.team);
        }
    } else {
        b.stuckTimer = 0.0f;
    }

    // ---- aiming & shooting ---------------------------------------------------
    float targetAngle;
    if (seesTarget) {
        // Bots lead their shots a little (imperfectly).
        float t = dist / kind.bulletSpeed * RandF(0.3f, 0.8f);
        Vec2f aimPoint = tgt->pos + tgt->vel * t;
        targetAngle = AngleOf(aimPoint - bot.pos) + b.aimError;
    } else if (LengthSq(bot.vel) > 100.0f) {
        targetAngle = AngleOf(bot.vel);
    } else {
        targetAngle = bot.aimAngle;
    }
    bot.aimAngle = RotateTowards(bot.aimAngle, targetAngle, kBotTurnSpeed * dt);

    bool aimedWell = std::fabs(WrapAngle(targetAngle - bot.aimAngle)) < 0.15f;
    bool inRange = dist < kind.bulletSpeed * kind.bulletLife * 0.9f;
    bool mayShoot = !botsPeaceful && !training;
    if (seesTarget && inRange && mayShoot && b.reaction <= 0.0f && b.fireCooldown <= 0.0f && aimedWell &&
        bot.reloadTimer <= 0.0f && bot.ammo > 0) {
        int damage = std::max(1, static_cast<int>(std::lround(kind.damage * diff.damage)));
        FireBullets(slot, bot.aimAngle, kind.bulletSpeed, damage, kind.pellets, kind.spread, kind.bulletLife, false);
        Emit(GameEventType::Shot, bot.pos, bot.weapon);
        b.fireCooldown = RandF(kind.fireMin, kind.fireMax) * diff.fireDelay;
        b.aimError     = RandF(-0.12f, 0.12f) * diff.aimError;
        if (--bot.ammo <= 0) bot.reloadTimer = kBotReload;
    }
}

// -----------------------------------------------------------------------------
//  Bullets & damage
// -----------------------------------------------------------------------------

void Game::FireBullets(int slot, float angle, float speed, int damage, int pellets, float spread,
                       float life, bool heavy) {
    const gm::Entity& e = mem_.entities[slot];
    if (mem_.buffs[slot].damageTime > 0.0f) damage *= 2;
    for (int i = 0; i < pellets; ++i) {
        float a = angle;
        if (pellets > 1) a += spread * (static_cast<float>(i) / (pellets - 1) - 0.5f) + RandF(-0.03f, 0.03f);
        else if (spread > 0.0f) a += RandF(-spread * 0.5f, spread * 0.5f);

        Bullet bl;
        bl.active  = true;
        Vec2f dir  = FromAngle(a);
        // Spawn at the center so bullets can not start inside a wall; they
        // can not hit the shooter (owner check).
        bl.pos     = e.pos;
        bl.prevPos = e.pos;
        bl.vel     = dir * speed * (pellets > 1 ? RandF(0.9f, 1.05f) : 1.0f);
        bl.life    = life;
        bl.owner   = slot;
        bl.team    = e.team;
        bl.damage  = damage;
        bl.heavy   = heavy;

        // Reuse a free slot if there is one.
        auto it = std::find_if(bullets_.begin(), bullets_.end(), [](const Bullet& x) { return !x.active; });
        if (it != bullets_.end()) *it = bl;
        else bullets_.push_back(bl);
    }
    Vec2f dir = FromAngle(angle);
    effects_.push_back({EffectType::Muzzle, e.pos + dir * (e.radius + 8.0f), angle, 0.0f,
                        heavy ? 0.1f : 0.06f, e.team, heavy ? 16.0f : 9.0f});
}

void Game::UpdateBullets(float dt) {
    for (Bullet& bl : bullets_) {
        if (!bl.active) continue;
        bl.life -= dt;
        if (bl.life <= 0.0f) { bl.active = false; continue; }

        bl.prevPos = bl.pos;
        Vec2f next = bl.pos + bl.vel * dt;

        // Find the earliest thing hit along prevPos -> next.
        float bestT = 2.0f;
        int   hitEntity = -1;
        for (uint32_t i = 0; i < mem_.obstacleCount; ++i) {
            float t;
            if (SegmentVsBox(bl.pos, next, mem_.obstacles[i], t) && t < bestT) {
                bestT = t;
                hitEntity = -1;
            }
        }
        for (int i = 0; i < gm::kMaxEntities; ++i) {
            const gm::Entity& e = mem_.entities[i];
            if (!e.active || !e.alive || i == bl.owner || e.team == bl.team) continue;
            float t;
            if (SegmentVsCircle(bl.pos, next, e.pos, e.radius + kBulletRadius, t) && t < bestT) {
                bestT = t;
                hitEntity = i;
            }
        }
        int hitBarrel = -1;
        for (int i = 0; i < gm::kMaxBarrels; ++i) {
            if (!mem_.barrels[i].alive) continue;
            float t;
            if (SegmentVsCircle(bl.pos, next, mem_.barrels[i].pos, kBarrelRadius + kBulletRadius, t) && t < bestT) {
                bestT = t;
                hitEntity = -1;
                hitBarrel = i;
            }
        }
        bool outside = next.x < 0 || next.y < 0 || next.x > kArenaW || next.y > kArenaH;

        if (bestT <= 1.0f) {
            Vec2f hitPos = Lerp(bl.pos, next, bestT);
            float angle = AngleOf(bl.vel);
            if (hitEntity >= 0) {
                ApplyDamage(hitEntity, bl.owner, bl.damage, hitPos, angle);
            } else if (hitBarrel >= 0) {
                effects_.push_back({EffectType::Spark, hitPos, angle, 0.0f, 0.25f, bl.team});
                DamageBarrel(hitBarrel, bl.owner, bl.damage);
            } else {
                effects_.push_back({EffectType::Spark, hitPos, angle, 0.0f, 0.25f, bl.team});
                if (bl.owner == static_cast<int>(mem_.localPlayerIndex)) Emit(GameEventType::WallHit, hitPos);
            }
            bl.pos = hitPos;
            bl.active = false;
        } else if (outside) {
            bl.active = false;
        } else {
            bl.pos = next;
        }
    }
}

void Game::Announce(const std::string& text) {
    announcement_.text = text;
    announcement_.time = 0.0f;
}

void Game::ApplyDamage(int victim, int attacker, int damage, Vec2f hitPos, float angle, const char* cause) {
    if (mem_.match.state != gm::MATCH_PLAYING) return;
    gm::Entity& v = mem_.entities[victim];
    if (!v.alive) return;
    bool victimIsPlayer = victim == static_cast<int>(mem_.localPlayerIndex);
    bool attackerIsPlayer = attacker == static_cast<int>(mem_.localPlayerIndex);

    if (victimIsPlayer && (godMode || spawnProtection_ > 0.0f || settings_.mode == gm::MODE_TRAINING)) damage = 0;

    // Where did it come from? (red arc on the HUD)
    if (victimIsPlayer && damage > 0) {
        Vec2f from = attacker >= 0 && attacker != victim ? mem_.entities[attacker].pos : hitPos;
        damageIndicators_.push_back({AngleOf(from - v.pos), 0.0f});
    }

    // Shield absorbs damage first.
    gm::EntityBuffs& buff = mem_.buffs[victim];
    if (buff.shield > 0 && damage > 0) {
        int absorbed = std::min(buff.shield, damage);
        buff.shield -= absorbed;
        damage -= absorbed;
    }

    effects_.push_back({EffectType::Blood, hitPos, angle, 0.0f, 0.3f, v.team});
    hitFlash_[victim] = 0.1f;
    if (attackerIsPlayer) {
        ++shotsHit;
        hitMarker_ = 0.15f;
        texts_.push_back({hitPos, 0.0f, damage});
        Emit(GameEventType::PlayerHitEnemy, hitPos);
        if (settings_.mode == gm::MODE_TRAINING && victim == 1 && !training_.targetHit) {
            training_.targetHit = true;
            training_.totalReaction += training_.targetAge;
            training_.reactions++;
        }
    }
    if (victimIsPlayer && damage > 0) {
        damageFlash_ = 0.25f;
        Emit(GameEventType::PlayerHurt, hitPos);
    }

    v.health -= damage;
    if (v.health > 0) return;

    // ---- kill ----
    v.health = 0;
    v.alive = 0;
    v.vel = {0, 0};
    v.reloadTimer = 0.0f;
    v.deaths++;
    v.respawnTimer = victimIsPlayer ? kPlayerRespawn : kBotRespawn;
    if (attacker >= 0 && attacker != victim) mem_.entities[attacker].kills++;
    effects_.push_back({EffectType::Death, v.pos, angle, 0.0f, 0.6f, v.team, v.radius});
    effects_.push_back({EffectType::Corpse, v.pos, angle, 0.0f, 8.0f, v.team, v.radius});
    Emit(victimIsPlayer ? GameEventType::PlayerDied : (attackerIsPlayer ? GameEventType::PlayerKill
                                                                         : GameEventType::Kill),
         v.pos);

    std::string attackerName = attacker >= 0 ? mem_.entities[attacker].name : "?";
    bool involvesPlayer = victimIsPlayer || attackerIsPlayer;
    killFeed_.push_back({attackerName + "  >  " + v.name, 0.0f, involvesPlayer});
    if (killFeed_.size() > 6) killFeed_.erase(killFeed_.begin());

    if (victimIsPlayer) {
        killStreak_ = 0;
        multiKill_ = 0;
        // death recap
        std::string what = cause ? cause : (attacker >= 0 ? GetWeaponDef(mem_.entities[attacker].weapon).name : "?");
        if (attacker == victim) deathRecap_ = std::string("Killed by your own ") + what;
        else if (attacker >= 0)
            deathRecap_ = std::string("Killed by ") + mem_.entities[attacker].name + " (" +
                          KindName(mem_.entities[attacker].kind) + ") - " + what;
        else deathRecap_ = std::string("Killed by ") + what;
    } else if (attackerIsPlayer) {
        ++killStreak_;
        multiKill_ = multiKillTimer_ > 0.0f ? multiKill_ + 1 : 1;
        multiKillTimer_ = 3.0f;
        if (multiKill_ == 2) Announce("DOUBLE KILL");
        else if (multiKill_ == 3) Announce("TRIPLE KILL");
        else if (multiKill_ >= 4) Announce("RAMPAGE");
        else if (killStreak_ == 5) Announce("KILLING SPREE");
        else if (killStreak_ == 10) Announce("UNSTOPPABLE");
        else if (killStreak_ == 15) Announce("GODLIKE");
        else if (killStreak_ > 15 && killStreak_ % 5 == 0) Announce(std::to_string(killStreak_) + " KILL STREAK");
    }

    OnKill(victim, attacker);
}

void Game::ApplyPickup(int slot, uint32_t type) {
    gm::Entity& e = mem_.entities[slot];
    gm::EntityBuffs& b = mem_.buffs[slot];
    switch (type) {
        case gm::PICKUP_HEALTH:
            e.health = std::min(e.maxHealth, e.health + kHealthPackAmount * e.maxHealth / 100);
            break;
        case gm::PICKUP_SPEED:   b.speedTime = kBuffDuration; break;
        case gm::PICKUP_DAMAGE:  b.damageTime = kBuffDuration; break;
        case gm::PICKUP_SHIELD:  b.shield = kMaxShield; break;
        case gm::PICKUP_GRENADE: b.grenades = std::min(kMaxGrenadesHeld, b.grenades + 1); break;
        default: break;
    }
}

void Game::UpdatePickups(float dt) {
    for (int i = 0; i < gm::kMaxPickups; ++i) {
        gm::Pickup& p = mem_.pickups[i];
        if (p.pos.x == 0.0f && p.pos.y == 0.0f) continue;  // unused slot
        bool isHealth = p.type == gm::PICKUP_HEALTH;
        if (!p.available) {
            pickupTimers_[i] -= dt;
            if (pickupTimers_[i] <= 0.0f) {
                p.available = 1;
                if (!isHealth) p.type = RandomPowerup();  // power-ups come back as a random type
            }
            continue;
        }
        for (int e = 0; e < gm::kMaxEntities; ++e) {
            gm::Entity& ent = mem_.entities[e];
            if (!ent.active || !ent.alive) continue;
            if (Distance(ent.pos, p.pos) > ent.radius + kPickupRadius) continue;
            // Only take what is useful.
            if (isHealth && ent.health >= ent.maxHealth) continue;
            if (p.type == gm::PICKUP_GRENADE && mem_.buffs[e].grenades >= kMaxGrenadesHeld) continue;
            ApplyPickup(e, p.type);
            p.available = 0;
            pickupTimers_[i] = isHealth ? kHealthPackRespawn : kPowerupRespawn;
            effects_.push_back({EffectType::Pickup, p.pos, 0.0f, 0.0f, 0.5f, ent.team, static_cast<float>(p.type)});
            if (e == static_cast<int>(mem_.localPlayerIndex))
                Emit(isHealth ? GameEventType::Pickup : GameEventType::PowerUp, p.pos, p.type);
            break;
        }
    }
}

void Game::UpdateBuffs(float dt) {
    for (gm::EntityBuffs& b : mem_.buffs) {
        b.speedTime = std::max(0.0f, b.speedTime - dt);
        b.damageTime = std::max(0.0f, b.damageTime - dt);
    }
    for (DamageIndicator& d : damageIndicators_) d.time += dt;
    damageIndicators_.erase(std::remove_if(damageIndicators_.begin(), damageIndicators_.end(),
                                           [](const DamageIndicator& d) { return d.time > 1.2f; }),
                            damageIndicators_.end());
}

// -----------------------------------------------------------------------------
//  Grenades, explosions, barrels
// -----------------------------------------------------------------------------

bool Game::ThrowGrenade(int slot, Vec2f target) {
    gm::Entity& e = mem_.entities[slot];
    gm::EntityBuffs& b = mem_.buffs[slot];
    if (!e.alive || b.grenades <= 0) return false;

    int free = -1;
    for (int i = 0; i < gm::kMaxGrenades; ++i)
        if (!mem_.grenades[i].active) { free = i; break; }
    if (free < 0) return false;

    Vec2f to = target - e.pos;
    float dist = std::min(Length(to), kGrenadeMaxRange);
    Vec2f dir = LengthSq(to) > 1.0f ? Normalize(to) : FromAngle(e.aimAngle);
    // With friction the grenade travels v0 / k * (1 - e^(-k t)): pick v0 so
    // it stops at the target just before the fuse runs out.
    float travel = (1.0f - std::exp(-kGrenadeFriction * kGrenadeFuse)) / kGrenadeFriction;
    float v0 = dist / travel;

    gm::Grenade& g = mem_.grenades[free];
    g.pos = e.pos + dir * (e.radius + 6.0f);
    if (CircleHitsObstacle(g.pos, 6.0f)) g.pos = e.pos;
    g.vel = dir * v0;
    g.fuse = kGrenadeFuse;
    g.active = 1;
    g.team = e.team;
    g.owner = slot;
    b.grenades--;
    Emit(GameEventType::GrenadeThrow, e.pos);
    return true;
}

void Game::UpdateGrenades(float dt) {
    const float r = 6.0f;
    for (gm::Grenade& g : mem_.grenades) {
        if (!g.active) continue;
        g.vel *= std::exp(-kGrenadeFriction * dt);

        // Move one axis at a time and bounce off walls.
        Vec2f p = g.pos;
        p.x += g.vel.x * dt;
        if (CircleHitsObstacle(p, r) || p.x < r || p.x > kArenaW - r) { p.x = g.pos.x; g.vel.x *= -0.5f; }
        p.y += g.vel.y * dt;
        if (CircleHitsObstacle(p, r) || p.y < r || p.y > kArenaH - r) { p.y = g.pos.y; g.vel.y *= -0.5f; }
        g.pos = p;

        g.fuse -= dt;
        if (g.fuse <= 0.0f) {
            g.active = 0;
            Explode(g.pos, g.owner, g.team, kGrenadeDamage, kExplosionRadius, "Grenade");
        }
    }
}

void Game::Explode(Vec2f pos, int owner, uint32_t team, int damage, float radius, const char* cause) {
    effects_.push_back({EffectType::Explosion, pos, 0.0f, 0.0f, 0.6f, team, radius});
    effects_.push_back({EffectType::Scorch, pos, RandF(0, 6.28f), 0.0f, 14.0f, team, radius * 0.5f});
    Emit(GameEventType::Explosion, pos);

    for (int i = 0; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = mem_.entities[i];
        if (!e.active || !e.alive) continue;
        if (e.team == team && i != owner) continue;  // no friendly fire (neutral barrels hurt everyone)
        float d = std::max(0.0f, Distance(pos, e.pos) - e.radius);
        if (d >= radius || !LineOfSight(pos, e.pos)) continue;  // walls protect
        float falloff = 1.0f - d / radius;
        int dmg = static_cast<int>(damage * falloff * (i == owner ? 0.5f : 1.0f));
        if (dmg > 0) ApplyDamage(i, owner, dmg, e.pos, AngleOf(e.pos - pos), cause);
    }
    // Chain reaction: barrels in range explode too (next tick).
    for (int i = 0; i < gm::kMaxBarrels; ++i) {
        const gm::Barrel& b = mem_.barrels[i];
        if (b.alive && Distance(pos, b.pos) < radius + kBarrelRadius) DamageBarrel(i, owner, 1000);
    }
}

void Game::DamageBarrel(int barrel, int attacker, int damage) {
    gm::Barrel& b = mem_.barrels[barrel];
    if (!b.alive) return;
    b.health -= damage;
    barrelAttacker_[barrel] = attacker;
    if (b.health <= 0) barrelPending_[barrel] = true;
}

void Game::UpdateBarrels(float dt) {
    const uint32_t kNeutral = 0xFFFFFFFFu;
    for (int i = 0; i < gm::kMaxBarrels; ++i) {
        gm::Barrel& b = mem_.barrels[i];
        if (b.pos.x == 0.0f && b.pos.y == 0.0f) continue;  // unused slot
        if (barrelPending_[i]) {
            barrelPending_[i] = false;
            b.alive = 0;
            b.health = 0;
            barrelTimers_[i] = kBarrelRespawn;
            Explode(b.pos, barrelAttacker_[i], kNeutral, kBarrelDamage, kExplosionRadius * 1.1f, "Barrel explosion");
            continue;
        }
        if (!b.alive) {
            barrelTimers_[i] -= dt;
            if (barrelTimers_[i] > 0.0f) continue;
            // Don't respawn on top of someone.
            bool blocked = false;
            for (const gm::Entity& e : mem_.entities)
                if (e.active && e.alive && Distance(e.pos, b.pos) < e.radius + kBarrelRadius + 4.0f) blocked = true;
            if (!blocked) {
                b.alive = 1;
                b.health = kBarrelHealth;
            }
        }
    }
}

void Game::UpdateRespawns(float dt) {
    for (int i = 0; i < gm::kMaxEntities; ++i) {
        gm::Entity& e = mem_.entities[i];
        if (!e.active || e.alive) continue;
        bool isPlayer = i == static_cast<int>(mem_.localPlayerIndex);
        // Survival: enemies stay dead. Training: targets are spawned by UpdateMatch.
        if (!isPlayer && (settings_.mode == gm::MODE_SURVIVAL || settings_.mode == gm::MODE_TRAINING)) continue;
        e.respawnTimer -= dt;
        if (e.respawnTimer <= 0.0f) SpawnEntity(i);
    }
}

void Game::UpdateEffects(float dt) {
    for (Effect& fx : effects_) fx.time += dt;
    effects_.erase(std::remove_if(effects_.begin(), effects_.end(),
                                  [](const Effect& fx) { return fx.time >= fx.duration; }),
                   effects_.end());

    for (FloatingText& t : texts_) {
        t.time += dt;
        t.pos.y -= 40.0f * dt;
    }
    texts_.erase(std::remove_if(texts_.begin(), texts_.end(),
                                [](const FloatingText& t) { return t.time > 0.8f; }),
                 texts_.end());

    for (KillFeedEntry& k : killFeed_) k.time += dt;
    killFeed_.erase(std::remove_if(killFeed_.begin(), killFeed_.end(),
                                   [](const KillFeedEntry& k) { return k.time > 6.0f; }),
                    killFeed_.end());
}
