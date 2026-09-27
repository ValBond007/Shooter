#include "game.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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

// =============================================================================

Game::Game(gm::GameMemory& mem, uint32_t seed) : mem_(mem), rng_(seed) {
    mem_.difficulty = gm::DIFFICULTY_NORMAL;
    BuildArena();
    Reset();
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

void Game::SetDifficulty(uint32_t d) { mem_.difficulty = d > gm::DIFFICULTY_HARD ? gm::DIFFICULTY_NORMAL : d; }

void Game::BuildArena() {
    mem_.arenaSize = {kArenaW, kArenaH};
    const gm::Obstacle layout[] = {
        {1100, 700, 200, 200},                                             // center block
        {500, 350, 260, 50},   {1640, 350, 260, 50},                       // long covers
        {500, 1200, 260, 50},  {1640, 1200, 260, 50},
        {400, 600, 50, 400},   {1950, 600, 50, 400},                       // side walls
        {850, 250, 80, 80},    {1470, 250, 80, 80},                        // crates
        {850, 1270, 80, 80},   {1470, 1270, 80, 80},
        {1100, 300, 200, 40},  {1100, 1260, 200, 40},                      // mid bars
        {150, 150, 120, 120},  {2130, 150, 120, 120},                      // corners
        {150, 1330, 120, 120}, {2130, 1330, 120, 120},
        {750, 760, 60, 80},    {1590, 760, 60, 80},                        // small crates
    };
    mem_.obstacleCount = 0;
    for (const auto& o : layout) {
        if (mem_.obstacleCount >= static_cast<uint32_t>(gm::kMaxObstacles)) break;
        mem_.obstacles[mem_.obstacleCount++] = o;
    }

    // Health packs at fixed spots.
    const Vec2f packs[] = {
        {1200, 200}, {1200, 1400}, {300, 800}, {2100, 800}, {640, 640}, {1760, 960},
    };
    std::memset(mem_.pickups, 0, sizeof(mem_.pickups));
    int i = 0;
    for (Vec2f p : packs) {
        mem_.pickups[i].pos = p;
        mem_.pickups[i].type = gm::PICKUP_HEALTH;
        mem_.pickups[i].available = 1;
        ++i;
    }
}

void Game::Reset() {
    bullets_.clear();
    effects_.clear();
    texts_.clear();
    killFeed_.clear();
    events_.clear();
    shotsFired = shotsHit = 0;
    killStreak_ = multiKill_ = 0;
    announcement_ = Announcement{};

    mem_.localPlayerIndex = 0;

    for (int i = 0; i < gm::kMaxPickups; ++i) {
        pickupTimers_[i] = 0.0f;
        if (mem_.pickups[i].pos.x != 0.0f || mem_.pickups[i].pos.y != 0.0f) mem_.pickups[i].available = 1;
    }

    for (int i = 0; i < gm::kMaxEntities; ++i) {
        gm::Entity& e = mem_.entities[i];
        std::memset(&e, 0, sizeof(e));
        e.team = (i == 0) ? gm::TEAM_PLAYER : gm::TEAM_BOTS;
        const char* name = (i == 0) ? "You" : kBotNames[i - 1];
        std::snprintf(e.name, sizeof(e.name), "%s", name);
    }

    // Player first, then bots (bots spawn far away from the player).
    gm::Entity& p = mem_.entities[0];
    p.active = 1;
    p.kind = gm::KIND_PLAYER;
    p.weapon = gm::WEAPON_RIFLE;
    for (int w = 0; w < gm::kWeaponCount; ++w) playerAmmo_[w] = kWeapons[w].magSize;
    SpawnEntity(0);
    SetBotCount(botCount_);
}

void Game::SetBotCount(int count) {
    count = std::max(0, std::min(count, gm::kMaxEntities - 1));
    botCount_ = count;
    for (int i = 1; i < gm::kMaxEntities; ++i) {
        gm::Entity& e = mem_.entities[i];
        bool shouldBeActive = i <= count;
        if (shouldBeActive && !e.active) {
            e.active = 1;
            e.kills = e.deaths = 0;
            SpawnEntity(i);
        } else if (!shouldBeActive && e.active) {
            e.active = 0;
            e.alive = 0;
            e.health = 0;
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

Vec2f Game::FindSpawnPoint(bool farFromPlayer) {
    const float margin = 60.0f;
    Vec2f best{kArenaW * 0.5f, kArenaH * 0.25f};
    float bestScore = -1.0f;
    for (int attempt = 0; attempt < 60; ++attempt) {
        Vec2f p{RandF(margin, kArenaW - margin), RandF(margin, kArenaH - margin)};
        if (CircleHitsObstacle(p, 40.0f)) continue;

        // Score = distance to the nearest enemy of whoever spawns here.
        float nearest = 1e9f;
        for (int i = 0; i < gm::kMaxEntities; ++i) {
            const gm::Entity& e = mem_.entities[i];
            if (!e.active || !e.alive) continue;
            bool relevant = farFromPlayer ? (i == 0) : (i != 0);
            if (relevant) nearest = std::min(nearest, Distance(p, e.pos));
        }
        if (nearest > 600.0f) return p;
        if (nearest > bestScore) {
            bestScore = nearest;
            best = p;
        }
    }
    return best;
}

void Game::SpawnEntity(int slot) {
    gm::Entity& e = mem_.entities[slot];
    bool isPlayer = slot == static_cast<int>(mem_.localPlayerIndex);

    e.id           = nextId_++;
    e.alive        = 1;
    e.pos          = FindSpawnPoint(!isPlayer);
    e.vel          = {0, 0};
    e.aimAngle     = RandF(-kPi, kPi);
    e.respawnTimer = 0.0f;
    e.reloadTimer  = 0.0f;
    e.visible      = 0;
    hitFlash_[slot] = 0.0f;

    if (isPlayer) {
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
        // Pick a bot type: 60% soldier, 25% runner, 15% heavy.
        float r = RandF(0, 1);
        e.kind = r < 0.60f ? gm::KIND_SOLDIER : (r < 0.85f ? gm::KIND_RUNNER : gm::KIND_HEAVY);
        const BotKindDef& k = kBotKinds[e.kind];
        e.maxHealth = k.health;
        e.radius    = k.radius;
        e.weapon    = k.weapon;
        e.ammo      = k.magSize;

        BotBrain& b = brains_[slot];
        b = BotBrain{};
        b.baseSpeed     = RandF(k.speedMin, k.speedMax);
        b.strafeDir     = RandF(0, 1) < 0.5f ? -1.0f : 1.0f;
        b.preferredDist = RandF(k.distMin, k.distMax);
        b.fireCooldown  = RandF(0.5f, 1.2f);
        b.wanderTarget  = FindSpawnPoint(false);
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
    mem_.gameTime += dt;

    UpdatePlayer(input, dt);
    for (int i = 1; i < gm::kMaxEntities; ++i) {
        const gm::Entity& e = mem_.entities[i];
        if (e.active && e.alive) UpdateBot(i, dt);
    }
    UpdateBullets(dt);
    UpdatePickups(dt);
    UpdateRespawns(dt);
    UpdateEffects(dt);
    UpdateVisibility();

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

void Game::UpdatePlayer(const PlayerInput& input, float dt) {
    gm::Entity& p = Player();
    playerFireCooldown_ -= dt;
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

    // ---- movement -------------------------------------------------------------------
    Vec2f wish = Normalize(input.move) * kPlayerSpeed;
    p.vel = MoveTowards(p.vel, wish, kPlayerAccel * dt);
    Vec2f moved = MoveCircle(p.pos, p.vel * dt, p.radius);
    // Store the real velocity (blocked by walls = slower), like the bots do.
    p.vel = moved / dt;

    Vec2f toAim = input.aimWorld - p.pos;
    if (LengthSq(toAim) > 1.0f) p.aimAngle = AngleOf(toAim);

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

void Game::UpdateBot(int slot, float dt) {
    gm::Entity& bot = mem_.entities[slot];
    BotBrain&   b   = brains_[slot];
    const BotKindDef& kind = kBotKinds[bot.kind <= gm::KIND_HEAVY ? bot.kind : gm::KIND_SOLDIER];
    const DifficultyDef& diff = kDifficulties[mem_.difficulty <= 2 ? mem_.difficulty : 1];
    const gm::Entity& player = Player();

    b.decisionTimer -= dt;
    b.dashTimer     -= dt;
    b.dashCooldown  -= dt;
    b.fireCooldown  -= dt;
    b.reaction      -= dt;
    b.lastSeenTimer += dt;

    if (bot.reloadTimer > 0.0f) {
        bot.reloadTimer -= dt;
        if (bot.reloadTimer <= 0.0f) {
            bot.reloadTimer = 0.0f;
            bot.ammo = kind.magSize;
        }
    }

    Vec2f toPlayer = player.pos - bot.pos;
    float dist = Length(toPlayer);
    bool seesPlayer = player.alive && dist < kBotViewRange && LineOfSight(bot.pos, player.pos);
    if (seesPlayer) {
        if (b.lastSeenTimer > 1.0f) b.reaction = RandF(0.25f, 0.5f) * diff.reaction;  // just spotted
        b.lastSeenTimer = 0.0f;
        b.lastSeenPos = player.pos;
    }
    bool engaging = player.alive && b.lastSeenTimer < 3.0f;

    // Low on health? Go for a health pack.
    int pack = -1;
    if (bot.health < bot.maxHealth * 45 / 100) pack = NearestPickup(bot.pos, 1000.0f);

    // ---- movement decision -------------------------------------------------
    if (b.decisionTimer <= 0.0f) {
        b.decisionTimer = RandF(0.25f, 0.8f);
        if (RandF(0, 1) < 0.65f) b.strafeDir = -b.strafeDir;  // change direction a lot
        b.preferredDist = RandF(kind.distMin, kind.distMax);
        b.jitterAngle   = RandF(-0.6f, 0.6f);

        // Is the player aiming at us? -> more likely to dodge with a dash.
        float aimDiff = std::fabs(WrapAngle(player.aimAngle - AngleOf(bot.pos - player.pos)));
        float dashChance = (seesPlayer && aimDiff < 0.2f) ? kind.dashChance + 0.35f : kind.dashChance;
        if (b.dashCooldown <= 0.0f && RandF(0, 1) < dashChance) {
            b.dashTimer    = RandF(0.15f, 0.3f);
            b.dashCooldown = RandF(0.8f, 1.6f);
        }
        if (!engaging && Distance(bot.pos, b.wanderTarget) < 80.0f) {
            b.wanderTarget = FindSpawnPoint(false);
        }
    }

    Vec2f desired{0, 0};
    if (pack >= 0) {
        // Run to the health pack (still shooting if the player is visible).
        Vec2f to = mem_.pickups[pack].pos - bot.pos;
        desired = Normalize(Rotate(to, b.jitterAngle * 0.3f));
    } else if (engaging) {
        Vec2f target = seesPlayer ? player.pos : b.lastSeenPos;
        Vec2f to = target - bot.pos;
        float d = Length(to);
        Vec2f dir = Normalize(to);
        if (seesPlayer) {
            // Circle around the player at the preferred distance.
            // While reloading, back off a bit.
            float wanted = b.preferredDist + (bot.reloadTimer > 0.0f ? 200.0f : 0.0f);
            float radial = Clampf((d - wanted) / 150.0f, -1.0f, 1.0f);
            Vec2f tangent = Perp(dir) * b.strafeDir;
            desired = Normalize(Rotate(dir * radial + tangent, b.jitterAngle));
        } else {
            // Hunt: walk to where the player was last seen.
            desired = d > 30.0f ? Rotate(dir, b.jitterAngle * 0.5f) : Vec2f{0, 0};
        }
    } else {
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
            b.wanderTarget  = FindSpawnPoint(false);
        }
    } else {
        b.stuckTimer = 0.0f;
    }

    // ---- aiming & shooting ---------------------------------------------------
    float targetAngle;
    if (seesPlayer) {
        // Bots lead their shots a little (imperfectly).
        float t = dist / kind.bulletSpeed * RandF(0.3f, 0.8f);
        Vec2f aimPoint = player.pos + player.vel * t;
        targetAngle = AngleOf(aimPoint - bot.pos) + b.aimError;
    } else if (LengthSq(bot.vel) > 100.0f) {
        targetAngle = AngleOf(bot.vel);
    } else {
        targetAngle = bot.aimAngle;
    }
    bot.aimAngle = RotateTowards(bot.aimAngle, targetAngle, kBotTurnSpeed * dt);

    bool aimedWell = std::fabs(WrapAngle(targetAngle - bot.aimAngle)) < 0.15f;
    bool inRange = dist < kind.bulletSpeed * kind.bulletLife * 0.9f;
    if (seesPlayer && inRange && !botsPeaceful && b.reaction <= 0.0f && b.fireCooldown <= 0.0f && aimedWell &&
        bot.reloadTimer <= 0.0f && bot.ammo > 0) {
        int damage = std::max(1, static_cast<int>(std::lround(kind.damage * diff.damage)));
        FireBullets(slot, bot.aimAngle, kind.bulletSpeed, damage, kind.pellets, kind.spread, kind.bulletLife, false);
        Emit(GameEventType::Shot, bot.pos, bot.weapon);
        b.fireCooldown = RandF(kind.fireMin, kind.fireMax) * diff.fireDelay;
        b.aimError     = RandF(-0.12f, 0.12f) * diff.aimError;
        if (--bot.ammo <= 0) bot.reloadTimer = kBotReload;
    }
}

void Game::FireBullets(int slot, float angle, float speed, int damage, int pellets, float spread,
                       float life, bool heavy) {
    const gm::Entity& e = mem_.entities[slot];
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
        bool outside = next.x < 0 || next.y < 0 || next.x > kArenaW || next.y > kArenaH;

        if (bestT <= 1.0f) {
            Vec2f hitPos = Lerp(bl.pos, next, bestT);
            float angle = AngleOf(bl.vel);
            if (hitEntity >= 0) {
                ApplyDamage(hitEntity, bl.owner, bl.damage, hitPos, angle);
            } else {
                effects_.push_back({EffectType::Spark, hitPos, angle, 0.0f, 0.25f, bl.team});
                if (bl.team == gm::TEAM_PLAYER) Emit(GameEventType::WallHit, hitPos);
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

void Game::ApplyDamage(int victim, int attacker, int damage, Vec2f hitPos, float angle) {
    gm::Entity& v = mem_.entities[victim];
    bool victimIsPlayer = victim == static_cast<int>(mem_.localPlayerIndex);
    bool attackerIsPlayer = attacker == static_cast<int>(mem_.localPlayerIndex);

    if (victimIsPlayer && (godMode || spawnProtection_ > 0.0f)) damage = 0;

    effects_.push_back({EffectType::Blood, hitPos, angle, 0.0f, 0.3f, v.team});
    hitFlash_[victim] = 0.1f;
    if (attackerIsPlayer) {
        ++shotsHit;
        hitMarker_ = 0.15f;
        texts_.push_back({hitPos, 0.0f, damage});
        Emit(GameEventType::PlayerHitEnemy, hitPos);
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
    if (attacker >= 0) mem_.entities[attacker].kills++;
    effects_.push_back({EffectType::Death, v.pos, angle, 0.0f, 0.6f, v.team, v.radius});
    effects_.push_back({EffectType::Corpse, v.pos, angle, 0.0f, 8.0f, v.team, v.radius});
    Emit(victimIsPlayer ? GameEventType::PlayerDied : (attackerIsPlayer ? GameEventType::PlayerKill
                                                                         : GameEventType::Kill),
         v.pos);

    std::string attackerName = attacker >= 0 ? mem_.entities[attacker].name : "?";
    killFeed_.push_back({attackerName + "  >  " + v.name, 0.0f, victimIsPlayer || attackerIsPlayer});
    if (killFeed_.size() > 6) killFeed_.erase(killFeed_.begin());

    if (victimIsPlayer) {
        killStreak_ = 0;
        multiKill_ = 0;
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
}

void Game::UpdatePickups(float dt) {
    for (int i = 0; i < gm::kMaxPickups; ++i) {
        gm::Pickup& p = mem_.pickups[i];
        if (p.pos.x == 0.0f && p.pos.y == 0.0f) continue;  // unused slot
        if (!p.available) {
            pickupTimers_[i] -= dt;
            if (pickupTimers_[i] <= 0.0f) p.available = 1;
            continue;
        }
        for (int e = 0; e < gm::kMaxEntities; ++e) {
            gm::Entity& ent = mem_.entities[e];
            if (!ent.active || !ent.alive || ent.health >= ent.maxHealth) continue;
            if (Distance(ent.pos, p.pos) > ent.radius + kPickupRadius) continue;
            ent.health = std::min(ent.maxHealth, ent.health + kHealthPackAmount * ent.maxHealth / 100);
            p.available = 0;
            pickupTimers_[i] = kHealthPackRespawn;
            effects_.push_back({EffectType::Pickup, p.pos, 0.0f, 0.0f, 0.5f, ent.team});
            if (e == static_cast<int>(mem_.localPlayerIndex)) Emit(GameEventType::Pickup, p.pos);
            break;
        }
    }
}

void Game::UpdateRespawns(float dt) {
    for (int i = 0; i < gm::kMaxEntities; ++i) {
        gm::Entity& e = mem_.entities[i];
        if (!e.active || e.alive) continue;
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
