#include "game.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265358979f;

// ---- tuning -------------------------------------------------------------------
constexpr float kArenaW = 2400.0f;
constexpr float kArenaH = 1600.0f;

constexpr float kPlayerRadius      = 18.0f;
constexpr float kPlayerSpeed       = 290.0f;
constexpr float kPlayerAccel       = 3200.0f;
constexpr int   kPlayerMaxHealth   = 100;
constexpr float kPlayerFireDelay   = 0.12f;   // seconds between shots
constexpr float kPlayerBulletSpeed = 1300.0f;
constexpr int   kPlayerDamage      = 25;      // 4 hits to kill a bot
constexpr float kPlayerRespawn     = 3.0f;

constexpr float kBotRadius      = 18.0f;
constexpr float kBotAccel       = 2200.0f;
constexpr int   kBotMaxHealth   = 100;
constexpr float kBotBulletSpeed = 750.0f;
constexpr int   kBotDamage      = 5;
constexpr float kBotRespawn     = 2.5f;
constexpr float kBotViewRange   = 1100.0f;
constexpr float kBotTurnSpeed   = 7.0f;       // radians / second

constexpr float kBulletLife   = 1.6f;
constexpr float kBulletRadius = 3.0f;

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

// =============================================================================

Game::Game(gm::GameMemory& mem, uint32_t seed) : mem_(mem), rng_(seed) {
    BuildArena();
    Reset();
}

float Game::RandF(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng_);
}

int Game::RandI(int lo, int hi) {
    return std::uniform_int_distribution<int>(lo, hi)(rng_);
}

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
}

void Game::Reset() {
    bullets_.clear();
    effects_.clear();
    texts_.clear();
    killFeed_.clear();
    shotsFired = shotsHit = 0;

    mem_.localPlayerIndex = 0;
    mem_.bulletSpeed = kPlayerBulletSpeed;

    for (int i = 0; i < gm::kMaxEntities; ++i) {
        gm::Entity& e = mem_.entities[i];
        std::memset(&e, 0, sizeof(e));
        e.team = (i == 0) ? gm::TEAM_PLAYER : gm::TEAM_BOTS;
        const char* name = (i == 0) ? "You" : kBotNames[i - 1];
        std::snprintf(e.name, sizeof(e.name), "%s", name);
    }

    // Player first, then bots (bots spawn far away from the player).
    mem_.entities[0].active = 1;
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
    e.maxHealth    = isPlayer ? kPlayerMaxHealth : kBotMaxHealth;
    e.health       = e.maxHealth;
    e.radius       = isPlayer ? kPlayerRadius : kBotRadius;
    e.pos          = FindSpawnPoint(!isPlayer);
    e.vel          = {0, 0};
    e.aimAngle     = RandF(-kPi, kPi);
    e.respawnTimer = 0.0f;
    e.visible      = 0;

    if (!isPlayer) {
        BotBrain& b = brains_[slot];
        b = BotBrain{};
        b.baseSpeed     = RandF(220.0f, 270.0f);
        b.strafeDir     = RandF(0, 1) < 0.5f ? -1.0f : 1.0f;
        b.preferredDist = RandF(220.0f, 480.0f);
        b.fireCooldown  = RandF(0.5f, 1.2f);
        b.wanderTarget  = FindSpawnPoint(false);
    } else {
        playerFireCooldown_ = 0.0f;
    }
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
    UpdateRespawns(dt);
    UpdateEffects(dt);
    UpdateVisibility();

    hitMarker_   = std::max(0.0f, hitMarker_ - dt);
    damageFlash_ = std::max(0.0f, damageFlash_ - dt);
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

    Vec2f wish = Normalize(input.move) * kPlayerSpeed;
    p.vel = MoveTowards(p.vel, wish, kPlayerAccel * dt);
    Vec2f moved = MoveCircle(p.pos, p.vel * dt, p.radius);
    // Store the real velocity (blocked by walls = slower), like the bots do.
    p.vel = moved / dt;

    Vec2f toAim = input.aimWorld - p.pos;
    if (LengthSq(toAim) > 1.0f) p.aimAngle = AngleOf(toAim);

    if (input.shoot && playerFireCooldown_ <= 0.0f) {
        FireBullet(0, p.aimAngle, kPlayerBulletSpeed, kPlayerDamage);
        playerFireCooldown_ = kPlayerFireDelay;
        ++shotsFired;
    }
}

void Game::UpdateBot(int slot, float dt) {
    gm::Entity& bot = mem_.entities[slot];
    BotBrain&   b   = brains_[slot];
    const gm::Entity& player = Player();

    b.decisionTimer -= dt;
    b.dashTimer     -= dt;
    b.dashCooldown  -= dt;
    b.fireCooldown  -= dt;
    b.reaction      -= dt;
    b.lastSeenTimer += dt;

    Vec2f toPlayer = player.pos - bot.pos;
    float dist = Length(toPlayer);
    bool seesPlayer = player.alive && dist < kBotViewRange && LineOfSight(bot.pos, player.pos);
    if (seesPlayer) {
        if (b.lastSeenTimer > 1.0f) b.reaction = RandF(0.25f, 0.5f);  // just spotted
        b.lastSeenTimer = 0.0f;
        b.lastSeenPos = player.pos;
    }
    bool engaging = player.alive && b.lastSeenTimer < 3.0f;

    // ---- movement decision -------------------------------------------------
    if (b.decisionTimer <= 0.0f) {
        b.decisionTimer = RandF(0.25f, 0.8f);
        if (RandF(0, 1) < 0.65f) b.strafeDir = -b.strafeDir;  // change direction a lot
        b.preferredDist = RandF(200.0f, 480.0f);
        b.jitterAngle   = RandF(-0.6f, 0.6f);

        // Is the player aiming at us? -> more likely to dodge with a dash.
        float aimDiff = std::fabs(WrapAngle(player.aimAngle - AngleOf(bot.pos - player.pos)));
        float dashChance = (seesPlayer && aimDiff < 0.2f) ? 0.55f : 0.2f;
        if (b.dashCooldown <= 0.0f && RandF(0, 1) < dashChance) {
            b.dashTimer    = RandF(0.15f, 0.3f);
            b.dashCooldown = RandF(0.8f, 1.6f);
        }
        if (!engaging && Distance(bot.pos, b.wanderTarget) < 80.0f) {
            b.wanderTarget = FindSpawnPoint(false);
        }
    }

    Vec2f desired{0, 0};
    if (engaging) {
        Vec2f target = seesPlayer ? player.pos : b.lastSeenPos;
        Vec2f to = target - bot.pos;
        float d = Length(to);
        Vec2f dir = Normalize(to);
        if (seesPlayer) {
            // Circle around the player at the preferred distance.
            float radial = Clampf((d - b.preferredDist) / 150.0f, -1.0f, 1.0f);
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
        if (d < 70.0f && d > 1e-3f) desired += away / d * ((70.0f - d) / 70.0f) * 1.5f;
    }
    desired = Normalize(desired);

    float speed = b.baseSpeed * (b.dashTimer > 0.0f ? 2.3f : 1.0f);
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
        float t = dist / kBotBulletSpeed * RandF(0.3f, 0.8f);
        Vec2f aimPoint = player.pos + player.vel * t;
        targetAngle = AngleOf(aimPoint - bot.pos) + b.aimError;
    } else if (LengthSq(bot.vel) > 100.0f) {
        targetAngle = AngleOf(bot.vel);
    } else {
        targetAngle = bot.aimAngle;
    }
    bot.aimAngle = RotateTowards(bot.aimAngle, targetAngle, kBotTurnSpeed * dt);

    bool aimedWell = std::fabs(WrapAngle(targetAngle - bot.aimAngle)) < 0.15f;
    if (seesPlayer && !botsPeaceful && b.reaction <= 0.0f && b.fireCooldown <= 0.0f && aimedWell) {
        FireBullet(slot, bot.aimAngle, kBotBulletSpeed, kBotDamage);
        b.fireCooldown = RandF(0.55f, 1.1f);
        b.aimError     = RandF(-0.12f, 0.12f);
    }
}

void Game::FireBullet(int slot, float angle, float speed, int damage) {
    const gm::Entity& e = mem_.entities[slot];
    Bullet bl;
    bl.active  = true;
    Vec2f dir  = FromAngle(angle);
    // Spawn at the center so bullets can not start inside a wall; they
    // can not hit the shooter (owner check).
    bl.pos     = e.pos;
    bl.prevPos = e.pos;
    bl.vel     = dir * speed;
    bl.life    = kBulletLife;
    bl.owner   = slot;
    bl.team    = e.team;
    bl.damage  = damage;

    // Reuse a free slot if there is one.
    auto it = std::find_if(bullets_.begin(), bullets_.end(), [](const Bullet& x) { return !x.active; });
    if (it != bullets_.end()) *it = bl;
    else bullets_.push_back(bl);

    effects_.push_back({EffectType::Muzzle, e.pos + dir * (e.radius + 8.0f), angle, 0.0f, 0.06f, e.team});
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

void Game::ApplyDamage(int victim, int attacker, int damage, Vec2f hitPos, float angle) {
    gm::Entity& v = mem_.entities[victim];
    bool victimIsPlayer = victim == static_cast<int>(mem_.localPlayerIndex);
    bool attackerIsPlayer = attacker == static_cast<int>(mem_.localPlayerIndex);

    if (victimIsPlayer && godMode) damage = 0;

    effects_.push_back({EffectType::Blood, hitPos, angle, 0.0f, 0.3f, v.team});
    if (attackerIsPlayer) {
        ++shotsHit;
        hitMarker_ = 0.15f;
        texts_.push_back({hitPos, 0.0f, damage});
    }
    if (victimIsPlayer && damage > 0) damageFlash_ = 0.25f;

    v.health -= damage;
    if (v.health > 0) return;

    // ---- kill ----
    v.health = 0;
    v.alive = 0;
    v.vel = {0, 0};
    v.deaths++;
    v.respawnTimer = victimIsPlayer ? kPlayerRespawn : kBotRespawn;
    if (attacker >= 0) mem_.entities[attacker].kills++;
    effects_.push_back({EffectType::Death, v.pos, angle, 0.0f, 0.6f, v.team});

    std::string attackerName = attacker >= 0 ? mem_.entities[attacker].name : "?";
    killFeed_.push_back({attackerName + "  >  " + v.name, 0.0f, victimIsPlayer || attackerIsPlayer});
    if (killFeed_.size() > 6) killFeed_.erase(killFeed_.begin());
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
